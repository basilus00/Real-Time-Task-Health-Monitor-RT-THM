/**
 * @file worker.c
 * @brief Worker process implementation for RT-THM
 * @details Each worker simulates tasks, updates shared memory stats,
 * and responds to pause/resume commands from the supervisor.
 */

#include "project.h"
#include "worker.h"
#include "ipc.h"
#include "logger.h"
#include "signals.h"

/**
 * @brief Main entry point for worker process
 * @param id Worker identifier (0 to MAX_WORKERS-1)
 * @details Infinite loop that:
 * - Checks for pause/resume commands from parent via shared memory
 * - Simulates work with random sleep (1-3s normal, 5s paused)
 * - Updates shared memory with health stats atomically
 * - Simulates random crash (5% probability) for testing auto-restart
 * @note This function never returns normally; calls exit() on crash
 * @warning Must be called only in child process after fork()
 */
void worker_process(int id) {
    // Register signal handlers for this worker
    signal(SIGUSR1, worker_signal_handler);  // Pause command
    signal(SIGUSR2, worker_signal_handler);  // Resume command
    signal(SIGTERM, cleanup);                 // Graceful shutdown
    
    srand(time(NULL) ^ (getpid() << 16));
    
    log_event("INFO", "Worker %d started (PID: %d)", id, getpid());
    printf("[WORKER %d] Démarré (PID: %d)\n", id, getpid());
    
    int paused = 0;  // Local flag for pause state
    
    while (1) {
        // === CHECK FOR COMMANDS FROM PARENT ===
        sem_wait_op(semid);
        int cmd = shared_stats[id].command;
        if (cmd == CMD_PAUSE) {
            paused = 1;
            shared_stats[id].command = CMD_NONE;  // Acknowledge
            log_event("DEBUG", "Worker %d: PAUSE command received", id);
        } else if (cmd == CMD_RESUME) {
            paused = 0;
            shared_stats[id].command = CMD_NONE;  // Acknowledge
            log_event("DEBUG", "Worker %d: RESUME command received", id);
        }
        sem_signal_op(semid);
        // === END COMMAND CHECK ===
        
        // Simulate work (respect pause state)
        int work_time = (rand() % 3) + 1;
        if (paused) {
            work_time = 5;  // Slower in pause mode (energy saving)
            log_event("DEBUG", "Worker %d: Paused mode (sleep %ds)", id, work_time);
        }
        sleep(work_time);
        
        // === PROTECTED WRITE TO SHARED MEMORY ===
        sem_wait_op(semid);
        
        shared_stats[id].pid = getpid();
        shared_stats[id].tasks_completed++;
        shared_stats[id].health_score = rand() % 100;
        shared_stats[id].last_update = time(NULL);
        
        // Simulate random crash (5% chance)
        if (shared_stats[id].health_score < 5) {
            log_event("ERROR", "Worker %d: Critical error! Crashing...", id);
            printf("[WORKER %d] Erreur critique ! Crash...\n", id);
            sem_signal_op(semid);  // Unlock before exit
            exit(EXIT_FAILURE);
        }
        
        sem_signal_op(semid);
        // === END PROTECTED WRITE ===
    }
}