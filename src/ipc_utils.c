/**
 * @file ipc_utils.c
 * @brief IPC utilities implementation (Shared Memory & Semaphores)
 * @details Thread-safe operations for POSIX IPC with error handling and logging.
 */

#include "project.h"
#include "ipc.h"
#include "logger.h"

/**
 * @brief Acquire semaphore lock (P operation)
 * @param semid Semaphore identifier
 * @note Blocks until available; logs error and exits on failure
 */
void sem_wait_op(int semid) {
    struct sembuf sb = {0, -1, 0};  // P operation
    if (semop(semid, &sb, 1) == -1) {
        log_event("ERROR", "sem_wait failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Release semaphore lock (V operation)
 * @param semid Semaphore identifier
 * @note Logs error and exits on failure
 */
void sem_signal_op(int semid) {
    struct sembuf sb = {0, 1, 0};  // V operation
    if (semop(semid, &sb, 1) == -1) {
        log_event("ERROR", "sem_signal failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Create and initialize binary semaphore
 * @return Semaphore ID on success, -1 on error
 * @note Initialized to 1 (unlocked); logs creation on success
 */
int create_semaphore(void) {
    int sid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    if (sid == -1) {
        log_event("ERROR", "semget failed: %s", strerror(errno));
        return -1;
    }
    
    union semun arg;
    arg.val = 1;  // Initialize to 1 (unlocked)
    if (semctl(sid, 0, SETVAL, arg) == -1) {
        log_event("ERROR", "semctl SETVAL failed: %s", strerror(errno));
        return -1;
    }
    
    log_event("INFO", "Semaphore created (ID: %d)", sid);
    return sid;
}

/**
 * @brief Create shared memory segment for worker stats
 * @param num_workers Number of ProcessStat entries to allocate
 * @return Shared memory ID on success, -1 on error
 */
int create_shared_memory(size_t num_workers) {
    int id = shmget(IPC_PRIVATE, sizeof(ProcessStat) * num_workers, IPC_CREAT | 0666);
    if (id == -1) {
        log_event("ERROR", "shmget failed: %s", strerror(errno));
        return -1;
    }
    log_event("DEBUG", "Shared memory segment created (ID: %d)", id);
    return id;
}

/**
 * @brief Attach shared memory to process address space
 * @param shmid Shared memory ID from create_shared_memory()
 * @return Pointer to ProcessStat array, or NULL on error
 */
ProcessStat* attach_shared_memory(int shmid) {
    ProcessStat *stats = (ProcessStat *)shmat(shmid, NULL, 0);
    if (stats == (void *)-1) {
        log_event("ERROR", "shmat failed: %s", strerror(errno));
        return NULL;
    }
    log_event("DEBUG", "Shared memory attached at %p", (void *)stats);
    return stats;
}

/**
 * @brief Detach shared memory from process address space
 * @param stats Pointer returned by attach_shared_memory()
 * @return 0 on success, -1 on error
 */
int detach_shared_memory(ProcessStat *stats) {
    if (stats && stats != (void *)-1) {
        if (shmdt(stats) == -1) {
            log_event("ERROR", "shmdt failed: %s", strerror(errno));
            return -1;
        }
        log_event("DEBUG", "Shared memory detached");
        return 0;
    }
    return 0;
}

/**
 * @brief Mark shared memory segment for deletion
 * @param shmid Shared memory ID
 * @return 0 on success, -1 on error
 * @note Segment destroyed after all processes detach; ignores EINVAL (already removed)
 */
int remove_shared_memory(int shmid) {
    if (shmid != -1) {
        if (shmctl(shmid, IPC_RMID, NULL) == -1 && errno != EINVAL) {
            log_event("ERROR", "shmctl IPC_RMID failed: %s", strerror(errno));
            return -1;
        }
        log_event("DEBUG", "Shared memory segment removed");
        return 0;
    }
    return 0;
}

/**
 * @brief Initialize a worker's stats entry in shared memory
 * @param worker_id Worker index (0 to MAX_WORKERS-1)
 * @param pid Worker process ID
 * @note Must be called with semaphore held; sets last_update, is_slow, command
 */
void init_worker_stats(int worker_id, pid_t pid) {
    if (shared_stats && worker_id >= 0 && worker_id < MAX_WORKERS) {
        sem_wait_op(semid);
        shared_stats[worker_id].pid = pid;
        shared_stats[worker_id].last_update = time(NULL);
        shared_stats[worker_id].is_slow = 0;
        shared_stats[worker_id].command = CMD_NONE;
        sem_signal_op(semid);
    }
}