#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "project.h"
#include "ipc.h"
#include "logger.h"
#include "config.h"
#include "signals.h"
#include "worker.h"
#include "UI.h"

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdarg.h>

// ============ FORWARD DECLARATIONS ============
void check_worker_timeouts(void);

// ============ MAIN FUNCTION ============
int main(void) {
    pid_t pids[MAX_WORKERS];
    time_t last_timeout_check = 0;
    int running = 1;
    
    // 0. Initialize logger
    if (logger_init() != 0) {
        fprintf(stderr, "[MAIN] Failed to initialize logger\n");
        return EXIT_FAILURE;
    }
    
    // 1. Load configuration from file
    load_config(CONFIG_FILE);
    if (validate_config() != 0) {
        log_event("ERROR", "Configuration validation failed");
        return EXIT_FAILURE;
    }
    
    log_event("INFO", "=== RT-THM SYSTEM STARTED ===");
    log_event("INFO", "Config: workers=%d, timeout=%ds, refresh=%ds, log_level=%d",
             MAX_WORKERS, WORKER_TIMEOUT, REFRESH_RATE, LOG_LEVEL);
    
    // 2. Setup signal handlers
    if (register_signal_handlers() != 0) {
        log_event("ERROR", "Failed to register signal handlers");
        return EXIT_FAILURE;
    }
    
    // 3. Create shared memory
    shmid = create_shared_memory(MAX_WORKERS);
    if (shmid == -1) {
        log_event("ERROR", "Failed to create shared memory");
        return EXIT_FAILURE;
    }
    
    shared_stats = attach_shared_memory(shmid);
    if (!shared_stats) {
        log_event("ERROR", "Failed to attach shared memory");
        remove_shared_memory(shmid);
        return EXIT_FAILURE;
    }
    
    // Initialize shared memory
    memset(shared_stats, 0, sizeof(ProcessStat) * MAX_WORKERS);
    log_event("DEBUG", "Shared memory initialized");
    
    // 4. Create semaphore
    semid = create_semaphore();
    if (semid == -1) {
        log_event("ERROR", "Failed to create semaphore");
        detach_shared_memory(shared_stats);
        remove_shared_memory(shmid);
        return EXIT_FAILURE;
    }
    
    // 5. Launch worker processes
    for (int i = 0; i < MAX_WORKERS; i++) {
        pids[i] = fork();
        if (pids[i] == -1) {
            log_event("ERROR", "fork failed for worker %d: %s", i, strerror(errno));
            cleanup(0);
        }
        
        if (pids[i] == 0) {
            worker_process(i);
            exit(EXIT_SUCCESS);
        }
        
        init_worker_stats(i, pids[i]);
        log_event("INFO", "Launched worker %d (PID: %d)", i, pids[i]);
    }
    
    // 6. Initialize Ncurses UI
    if (ui_init() != 0) {
        log_event("ERROR", "Failed to initialize UI - falling back to console");
        printf("[SUPERVISEUR] Surveillance active (console mode)...\n");
        printf("[SUPERVISEUR] Config: %d workers, %ds timeout, %ds refresh\n\n", 
               MAX_WORKERS, WORKER_TIMEOUT, REFRESH_RATE);
    } else {
        ui_add_log("[SYSTEM] RT-THM Monitoring System Started");
        ui_add_log("[CONFIG] Workers=%d, Timeout=%ds, Refresh=%ds", 
                   MAX_WORKERS, WORKER_TIMEOUT, REFRESH_RATE);
        ui_add_log("[IPC] Shared memory and semaphore initialized");
    }
    
    // 7. Supervision loop with UI
    while (running) {
        // Handle user input (ncurses non-blocking)
        int ch = getch();
        switch (ch) {
            case 'q':
            case 'Q':
                log_event("INFO", "User requested shutdown via UI");
                ui_add_log("[USER] Shutdown requested - cleaning up...");
                ui_refresh();           // Final UI update
                ui_cleanup();           // ← Restore terminal FIRST (critical!)
                cleanup(SIGTERM);       // ← Then cleanup resources (calls exit())
                break;    
            case 'r':
            case 'R':
                log_event("INFO", "User requested manual restart of all workers");
                ui_add_log("[USER] Manual restart triggered");
                for (int i = 0; i < MAX_WORKERS; i++) {
                    if (pids[i] > 0) {
                        (void)kill(pids[i], SIGTERM);
                    }
                }
                break;
            case KEY_RESIZE:
                ui_handle_resize();  // Recreate windows for new size
                clearok(stdscr, TRUE);  // Force full screen clear on next draw
                break;
            case ERR:
                // No input available (non-blocking), continue normally
                break;
            default:
                // Ignore other keys
                break;
        }
        
        // Draw the dashboard
        if (LOG_LEVEL >= LOG_INFO) {
            ui_draw_dashboard();
        }
        
        time_t current_time = time(NULL);
        
        // Monitor each worker
        for (int i = 0; i < MAX_WORKERS; i++) {
            // PROTECTED READ with Semaphore
            sem_wait_op(semid);
            
            int health = shared_stats[i].health_score;
            
            // Log critical events to UI console
            if (health < 20 && !shared_stats[i].is_slow) {
                ui_add_log("[WORKER %d] Health CRITICAL: %d%%", i, health);
            }
            
            sem_signal_op(semid);
            // END PROTECTED READ
            
            // Check if worker is still alive (reap zombies)
            int status;
            pid_t result = waitpid(pids[i], &status, WNOHANG);
            
            if (result == -1) {
                log_event("ERROR", "waitpid failed for worker %d", i);
            } else if (result == pids[i]) {
                // Worker terminated
                if (WIFEXITED(status)) {
                    log_event("WARN", "Worker %d exited with code %d", 
                             i, WEXITSTATUS(status));
                    ui_add_log("[WORKER %d] Exited (code %d) - Restarting...", 
                              i, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    log_event("WARN", "Worker %d killed by signal %d", 
                             i, WTERMSIG(status));
                    ui_add_log("[WORKER %d] Killed by signal %d - Restarting...", 
                              i, WTERMSIG(status));
                }
                
                // Restart worker
                pids[i] = fork();
                if (pids[i] == -1) {
                    log_event("ERROR", "fork failed during restart of worker %d", i);
                    ui_add_log("[ERROR] Failed to restart worker %d", i);
                } else if (pids[i] == 0) {
                    worker_process(i);
                    exit(EXIT_SUCCESS);
                }
                
                // Update shared stats for new worker
                init_worker_stats(i, pids[i]);
                ui_add_log("[WORKER %d] Restarted (new PID: %d)", i, pids[i]);
                log_event("INFO", "Worker %d restarted (new PID: %d)", i, pids[i]);
            }
        }
        
        // Periodic timeout check
        if ((current_time - last_timeout_check) >= REFRESH_RATE) {
            check_worker_timeouts();
            last_timeout_check = current_time;
            
            if (LOG_LEVEL >= LOG_DEBUG) {
                ui_add_log("[STATUS] Health check completed at %ld", (long)current_time);
            }
        }
        
        // Refresh UI at configured rate
        ui_refresh();
        (void)usleep(100000);  // 100ms sleep to reduce CPU usage
    }
    
    // 8. Fallback cleanup (if loop exits without 'q')
    if (running == 0) {
        log_event("INFO", "=== SYSTEM SHUTDOWN ===");
        ui_cleanup();   // Restore terminal
        cleanup(SIGTERM);  // Cleanup resources
    }
    return 0;
}

// ============ TIMEOUT MONITORING ============
void check_worker_timeouts(void) {
    time_t current_time = time(NULL);
    
    for (int i = 0; i < MAX_WORKERS; i++) {
        sem_wait_op(semid);
        
        time_t last_update = shared_stats[i].last_update;
        int is_slow = shared_stats[i].is_slow;
        pid_t pid = shared_stats[i].pid;
        
        // Detect slow/unresponsive worker
        if (last_update > 0 && (current_time - last_update) > WORKER_TIMEOUT) {
            if (!is_slow) {
                shared_stats[i].is_slow = 1;
                log_event("WARN", "Worker %d (PID %d) TIMEOUT - %lds idle", 
                         i, pid, (long)(current_time - last_update));
                
                ui_add_log("[TIMEOUT] Worker %d unresponsive (%lds) - Sending SIGUSR1", 
                          i, (long)(current_time - last_update));
                
                send_signal_to_worker(pid, SIGUSR1);
                shared_stats[i].command = CMD_PAUSE;
            }
        } else if (last_update > 0 && is_slow) {
            // Worker recovered
            shared_stats[i].is_slow = 0;
            log_event("INFO", "Worker %d (PID %d) RECOVERED", i, pid);
            
            ui_add_log("[RECOVERED] Worker %d back to normal operation", i);
            
            send_signal_to_worker(pid, SIGUSR2);
            shared_stats[i].command = CMD_RESUME;
        }
        
        sem_signal_op(semid);
    }
}