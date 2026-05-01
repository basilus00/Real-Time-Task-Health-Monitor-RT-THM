/**
 * @file ipc.h
 * @brief IPC utilities for RT-THM (Shared Memory & Semaphores)
 * @details Provides thread-safe operations for inter-process communication
 * using POSIX shared memory and binary semaphores.
 */

#ifndef IPC_H
#define IPC_H

#include "project.h"

/**
 * @brief Acquire semaphore lock (P operation)
 * @param semid Semaphore identifier
 * @note Blocks until semaphore is available
 */
void sem_wait_op(int semid);

/**
 * @brief Release semaphore lock (V operation)
 * @param semid Semaphore identifier
 */
void sem_signal_op(int semid);

/**
 * @brief Create and initialize binary semaphore
 * @return Semaphore ID on success, -1 on error
 */
int create_semaphore(void);

/**
 * @brief Create shared memory segment for worker stats
 * @param num_workers Number of ProcessStat entries to allocate
 * @return Shared memory ID on success, -1 on error
 */
int create_shared_memory(size_t num_workers);

/**
 * @brief Attach shared memory to process address space
 * @param shmid Shared memory ID from create_shared_memory()
 * @return Pointer to ProcessStat array, or NULL on error
 */
ProcessStat* attach_shared_memory(int shmid);

/**
 * @brief Detach shared memory from process address space
 * @param stats Pointer returned by attach_shared_memory()
 * @return 0 on success, -1 on error
 */
int detach_shared_memory(ProcessStat *stats);

/**
 * @brief Mark shared memory segment for deletion
 * @param shmid Shared memory ID
 * @return 0 on success, -1 on error
 * @note Segment is destroyed after all processes detach
 */
int remove_shared_memory(int shmid);

/**
 * @brief Initialize a worker's stats entry in shared memory
 * @param worker_id Worker index (0 to MAX_WORKERS-1)
 * @param pid Worker process ID
 * @note Must be called with semaphore held
 */
void init_worker_stats(int worker_id, pid_t pid);

#endif // IPC_H