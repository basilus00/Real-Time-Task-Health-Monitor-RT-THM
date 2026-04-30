#include "project.h"
#include "signals.h"
#include "ipc.h"
#include "logger.h"

volatile sig_atomic_t cleanup_done = 0;

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
    // Async-signal-safe: only set flags, no complex operations
    // Actual behavior is checked in worker loop via shared_stats[i].command
    (void)sig;
}

void cleanup(int sig) {
    (void)sig;
    
    if (cleanup_done) return;
    cleanup_done = 1;
    
    log_event("INFO", "=== SYSTEM SHUTDOWN INITIATED ===");
    
    // Terminate all workers gracefully
    if (shared_stats) {
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (shared_stats[i].pid > 0) {
                kill(shared_stats[i].pid, SIGTERM);
                log_event("INFO", "Sent SIGTERM to worker %d (PID: %d)", i, shared_stats[i].pid);
            }
        }
    }
    
    usleep(100000);  // Brief wait for cleanup
    
    // Cleanup shared memory
    detach_shared_memory(shared_stats);
    shared_stats = NULL;
    
    remove_shared_memory(shmid);
    shmid = -1;
    
    // Cleanup semaphore
    if (semid != -1) {
        semctl(semid, 0, IPC_RMID);
        semid = -1;
        log_event("DEBUG", "Removed semaphore");
    }
    
    logger_close();
    
    printf("\n[SUPERVISEUR] ✅ Système arrêté proprement.\n");
    log_event("INFO", "=== SYSTEM SHUTDOWN COMPLETE ===\n");
    exit(EXIT_SUCCESS);
}

int register_signal_handlers(void) {
    if (signal(SIGINT, cleanup) == SIG_ERR) {
        log_event("ERROR", "Failed to register SIGINT handler");
        return -1;
    }
    if (signal(SIGTERM, cleanup) == SIG_ERR) {
        log_event("ERROR", "Failed to register SIGTERM handler");
        return -1;
    }
    return 0;
}