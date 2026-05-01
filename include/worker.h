/**
 * @file worker.h
 * @brief Worker process module for RT-THM
 * @details Implements the child process logic: task simulation, health updates, and signal handling.
 */

#ifndef WORKER_H
#define WORKER_H

#include "project.h"

/**
 * @brief Main entry point for worker process
 * @param id Worker identifier (0 to MAX_WORKERS-1)
 * @details Infinite loop that:
 * - Checks for pause/resume commands from parent
 * - Simulates work with random sleep intervals
 * - Updates shared memory with health stats
 * - Handles random crash simulation (5% probability)
 * @note This function never returns normally; calls exit() on crash
 * @warning Must be called only in child process after fork()
 */
void worker_process(int id);

#endif // WORKER_H