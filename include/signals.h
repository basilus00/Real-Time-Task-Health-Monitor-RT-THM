/**
 * @file signals.h
 * @brief Signal handling for RT-THM (SIGINT, SIGUSR1, SIGUSR2)
 * @details Manages cleanup, worker pause/resume, and timeout monitoring.
 */

#ifndef SIGNALS_H
#define SIGNALS_H

#include "project.h"

/**
 * @brief Cleanup handler for graceful shutdown
 * @param sig Signal number that triggered cleanup (SIGINT/SIGTERM)
 * @note Calls endwin() first to restore terminal, then frees IPC resources
 */
void cleanup(int sig);

/**
 * @brief Worker signal handler for SIGUSR1/SIGUSR2
 * @param sig Signal received (SIGUSR1=pause, SIGUSR2=resume)
 * @note Async-signal-safe: only sets flags, no complex operations
 */
void worker_signal_handler(int sig);

/**
 * @brief Send signal to a specific worker process
 * @param pid Target worker process ID
 * @param sig_num Signal to send (SIGUSR1, SIGUSR2, SIGTERM)
 * @return void (errors logged internally)
 */
void send_signal_to_worker(pid_t pid, int sig_num);

/**
 * @brief Register all signal handlers for the process
 * @return 0 on success, -1 on failure
 * @note Call once at startup before forking workers
 */
int register_signal_handlers(void);

/**
 * @brief Check for worker timeouts and send pause/resume signals
 * @details Monitors last_update timestamp; sends SIGUSR1 if idle > WORKER_TIMEOUT
 * @note Must be called periodically from main supervision loop
 */
void check_worker_timeouts(void);

#endif // SIGNALS_H