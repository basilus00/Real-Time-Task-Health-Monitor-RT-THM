/**
 * @file signals.c
 * @brief Signal handling implementation for RT-THM
 * @details Manages cleanup, worker pause/resume signals, and graceful shutdown.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "project.h"
#include "signals.h"
#include "ipc.h"
#include "logger.h"

/**
 * @brief Send signal to a specific worker process
 * @param pid Target worker process ID
 * @param sig_num Signal to send (SIGUSR1, SIGUSR2, SIGTERM)
 * @note Logs warning on failure; ignores ESRCH (process already exited)
 */
void send_signal_to_worker(pid_t pid, int sig_num) {
    if (pid > 0) {
        if (kill(pid, sig_num) == -1 && errno != ESRCH) {
            log_event("WARN", "kill(%d, %d) failed: %s", pid, sig_num, strerror(errno));
        } else {
            log_event("DEBUG", "Sent signal %d to PID %d", sig_num, pid);
        }
    }
}

/**
 * @brief Worker signal handler for SIGUSR1/SIGUSR2
 * @param sig Signal received
 * @note Async-signal-safe: minimal operations only; actual behavior handled in worker loop
 */
void worker_signal_handler(int sig) {
    (void)sig;
}

/**
 * @brief Cleanup handler for graceful system shutdown
 * @param sig Signal that triggered cleanup (SIGINT/SIGTERM)
 * @details Performs in order:
 * 1. Restores terminal via endwin()
 * 2. Sends SIGTERM to all workers
 * 3. Detaches and removes shared memory
 * 4. Removes semaphore
 * 5. Closes log file
 * 6. Exits via _exit() (signal-safe)
 * @note Uses cleanup_done flag to prevent double-execution
 * @warning Calls _exit() instead of exit() for signal-handler safety
 */
void cleanup(int sig) {
    (void)sig;
    
    if (cleanup_done) return;
    cleanup_done = 1;
    
    log_event("INFO", "=== SYSTEM SHUTDOWN INITIATED ===");
    
    // 1. FIRST: Restore terminal from ncurses mode
    endwin();
    
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
    _exit(EXIT_SUCCESS);
}

/**
 * @brief Wrapper signal handler for safe terminal restoration
 * @param sig Signal received (SIGINT/SIGTERM)
 * @note Calls endwin() immediately before delegating to cleanup()
 * @see cleanup()
 */
void safe_shutdown(int sig) {
    endwin();
    printf("\n[SUPERVISEUR] ⚠️  Interrupt received, shutting down...\n");
    cleanup(sig);
}

/**
 * @brief Register all signal handlers for the process
 * @return 0 on success, -1 on failure
 * @note Registers safe_shutdown for SIGINT and SIGTERM
 */
int register_signal_handlers(void) {
    if (signal(SIGINT, safe_shutdown) == SIG_ERR) {
        log_event("ERROR", "Failed to register SIGINT handler");
        return -1;
    }
    if (signal(SIGTERM, safe_shutdown) == SIG_ERR) {
        log_event("ERROR", "Failed to register SIGTERM handler");
        return -1;
    }
    return 0;
}