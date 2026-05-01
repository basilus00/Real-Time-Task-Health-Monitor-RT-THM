#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "project.h"
#include "signals.h"
#include "ipc.h"
#include "logger.h"

void send_signal_to_worker(pid_t pid, int sig_num) {
    if (pid > 0) {
        if (kill(pid, sig_num) == -1 && errno != ESRCH) {
            log_event("WARN", "kill(%d, %d) failed: %s", pid, sig_num, strerror(errno));
        } else {
            log_event("DEBUG", "Sent signal %d to PID %d", sig_num, pid);
        }
    }
}

void worker_signal_handler(int sig) {
    (void)sig;
}

void cleanup(int sig) {
    (void)sig;
    
    if (cleanup_done) return;
    cleanup_done = 1;
    
    log_event("INFO", "=== SYSTEM SHUTDOWN INITIATED ===");
    
    // 1. FIRST: Restore terminal from ncurses mode
    endwin();  // ← CRITICAL: Restore terminal BEFORE anything else
    
    // 2. Terminate all workers gracefully
    if (shared_stats) {
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (shared_stats[i].pid > 0) {
                kill(shared_stats[i].pid, SIGTERM);
                log_event("INFO", "Sent SIGTERM to worker %d (PID: %d)", 
                         i, shared_stats[i].pid);
            }
        }
    }
    
    usleep(100000);  // Brief wait for cleanup
    
    // 3. Cleanup shared memory
    if (shared_stats && shared_stats != (void *)-1) {
        shmdt(shared_stats);
        shared_stats = NULL;
        log_event("DEBUG", "Detached shared memory");
    }
    
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
        shmid = -1;
        log_event("DEBUG", "Removed shared memory segment");
    }
    
    // 4. Cleanup semaphore
    if (semid != -1) {
        semctl(semid, 0, IPC_RMID);
        semid = -1;
        log_event("DEBUG", "Removed semaphore");
    }
    
    // 5. Close logger
    logger_close();
    
    // 6. Print final message (now terminal is restored)
    printf("\n[SUPERVISEUR] ✅ Système arrêté proprement.\n");
    log_event("INFO", "=== SYSTEM SHUTDOWN COMPLETE ===\n");
    
    // 7. Exit cleanly
    _exit(EXIT_SUCCESS);  // Use _exit() instead of exit() in signal handler
}


void safe_shutdown(int sig) {
    // Immediately restore terminal
    endwin();
    printf("\n[SUPERVISEUR] ⚠️  Interrupt received, shutting down...\n");
    
    // Now call the proper cleanup
    cleanup(sig);
}

int register_signal_handlers(void) {
    if (signal(SIGINT, safe_shutdown) == SIG_ERR) {  // ← Use safe_shutdown
        log_event("ERROR", "Failed to register SIGINT handler");
        return -1;
    }
    if (signal(SIGTERM, safe_shutdown) == SIG_ERR) {  // ← Use safe_shutdown
        log_event("ERROR", "Failed to register SIGTERM handler");
        return -1;
    }
    return 0;
}