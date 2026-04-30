#ifndef IPC_H
#define IPC_H

#include "project.h"

// Semaphore operations
void sem_wait_op(int semid);
void sem_signal_op(int semid);
int create_semaphore(void);

// Shared memory operations
int create_shared_memory(size_t num_workers);
ProcessStat* attach_shared_memory(int shmid);
int detach_shared_memory(ProcessStat *stats);
int remove_shared_memory(int shmid);

// Utility
void init_worker_stats(int worker_id, pid_t pid);

#endif // IPC_H