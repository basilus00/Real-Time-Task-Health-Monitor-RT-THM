#include "project.h"
#include "ipc.h"
#include "logger.h"

// ============ SEMAPHORE OPERATIONS ============

void sem_wait_op(int semid) {
    struct sembuf sb = {0, -1, 0};  // P operation
    if (semop(semid, &sb, 1) == -1) {
        log_event("ERROR", "sem_wait failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void sem_signal_op(int semid) {
    struct sembuf sb = {0, 1, 0};  // V operation
    if (semop(semid, &sb, 1) == -1) {
        log_event("ERROR", "sem_signal failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

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

// ============ SHARED MEMORY OPERATIONS ============

int create_shared_memory(size_t num_workers) {
    int id = shmget(IPC_PRIVATE, sizeof(ProcessStat) * num_workers, IPC_CREAT | 0666);
    if (id == -1) {
        log_event("ERROR", "shmget failed: %s", strerror(errno));
        return -1;
    }
    log_event("DEBUG", "Shared memory segment created (ID: %d)", id);
    return id;
}

ProcessStat* attach_shared_memory(int shmid) {
    ProcessStat *stats = (ProcessStat *)shmat(shmid, NULL, 0);
    if (stats == (void *)-1) {
        log_event("ERROR", "shmat failed: %s", strerror(errno));
        return NULL;
    }
    log_event("DEBUG", "Shared memory attached at %p", (void *)stats);
    return stats;
}

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

// Initialize a worker's stats entry
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