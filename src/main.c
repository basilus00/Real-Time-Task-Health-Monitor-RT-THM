#include "project.h"
#include "ipc.h"
#include "logger.h"
#include "config.h"
#include "signals.h"
#include "worker.h"

// ============ FORWARD DECLARATIONS ============
void check_worker_timeouts(void);

// ... rest of main.c ...
int main(void) {
    pid_t pids[MAX_WORKERS];
    time_t last_timeout_check = 0;
    
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
    log_event("INFO", "Config: workers=%d, timeout=%ds, refresh=%ds, log_level=%d",MAX_WORKERS, WORKER_TIMEOUT, REFRESH_RATE, LOG_LEVEL);
    
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
            // Child process - execute worker logic
            worker_process(i);
            exit(EXIT_SUCCESS);  // Should never reach
        }
        
        // Parent: initialize worker entry in shared memory
        init_worker_stats(i, pids[i]);
        log_event("INFO", "Launched worker %d (PID: %d)", i, pids[i]);
    }
    
    // 6. Supervision loop
    printf("[SUPERVISEUR] Surveillance active...\n");
    printf("[SUPERVISEUR] Config: %d workers, %ds timeout, %ds refresh\n\n", MAX_WORKERS, WORKER_TIMEOUT, REFRESH_RATE);
    
    while (1) {
        printf("\n========== Dashboard Temps Réel ==========\n");
        
        time_t current_time = time(NULL);
        
        for (int i = 0; i < MAX_WORKERS; i++) {
            // === PROTECTED READ ===
            sem_wait_op(semid);
            
            time_t last_update = shared_stats[i].last_update;
            int is_slow = shared_stats[i].is_slow;
            int tasks = shared_stats[i].tasks_completed;
            int health = shared_stats[i].health_score;
            pid_t pid = shared_stats[i].pid;
            time_t idle = (last_update > 0) ? (current_time - last_update) : 0;
            
            // Display with status indicator
            printf("Worker %d | PID: %-6d | Santé: %3d%% | Tâches: %3d | ",i, pid, health, tasks);
            
            if (is_slow) {
                printf("⚠️  SLOW (%lds)", (long)idle);
            } else if (idle > WORKER_TIMEOUT / 2) {
                printf("⏱  (%lds)", (long)idle);
            } else {
                printf("✓ OK");
            }
            printf("\n");
            
            sem_signal_op(semid);
            // === END PROTECTED READ ===
            
            // Check if worker is still alive (reap zombies)
            int status;
            pid_t result = waitpid(pids[i], &status, WNOHANG);
            
            if (result == -1) {
                log_event("ERROR", "waitpid failed for worker %d", i);
            } else if (result == pids[i]) {
                // Worker terminated
                if (WIFEXITED(status)) {
                    log_event("WARN", "Worker %d exited with code %d", i, WEXITSTATUS(status));
                    printf("[ALERTE] Worker %d exited (code %d). Relance...\n", i, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    log_event("WARN", "Worker %d killed by signal %d", i, WTERMSIG(status));
                    printf("[ALERTE] Worker %d killed by signal %d. Relance...\n", i, WTERMSIG(status));
                }
                
                // Restart worker
                pids[i] = fork();
                if (pids[i] == -1) {
                    log_event("ERROR", "fork failed during restart of worker %d", i);
                } else if (pids[i] == 0) {
                    worker_process(i);
                    exit(EXIT_SUCCESS);
                }
                
                // Update shared stats for new worker
                init_worker_stats(i, pids[i]);
                log_event("INFO", "Worker %d restarted (new PID: %d)", i, pids[i]);
            }
        }
        
        // Periodic timeout check
        if ((current_time - last_timeout_check) >= REFRESH_RATE) {
            check_worker_timeouts();
            last_timeout_check = current_time;
        }
        
        printf("==========================================\n");
        sleep(REFRESH_RATE);
    }
    
    return 0;
}

// ============ TIMEOUT MONITORING (called from main) ============
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
                log_event("WARN", "Worker %d (PID %d) TIMEOUT - %lds idle", i, pid, (long)(current_time - last_update));
                
                // Send SIGUSR1: "slow down / energy saving"
                send_signal_to_worker(pid, SIGUSR1);
                
                // Send PAUSE command via shared memory
                shared_stats[i].command = CMD_PAUSE;
            }
        } else if (last_update > 0 && is_slow) {
            // Worker recovered
            shared_stats[i].is_slow = 0;
            log_event("INFO", "Worker %d (PID %d) RECOVERED", i, pid);
            
            // Send SIGUSR2: "resume normal operation"
            send_signal_to_worker(pid, SIGUSR2);
            
            // Send RESUME command via shared memory
            shared_stats[i].command = CMD_RESUME;
        }
        
        sem_signal_op(semid);
    }
}