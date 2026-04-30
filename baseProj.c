#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#define MAX_WORKERS 3
#define SHM_SIZE 1024
#define WORKER_TIMEOUT 10  // Seconds before a worker is considered slow/hung
#define REFRESH_RATE 2     // Dashboard refresh rate in seconds

// Structure pour l'état des processus en mémoire partagée
typedef struct {
    int pid;
    int health_score;
    int tasks_completed;
    time_t last_update;  // Timestamp for timeout detection
    int is_slow;         // Flag: 1 if worker is slow, 0 if normal
} ProcessStat;

// Union required by semctl
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// Global variables
ProcessStat *shared_stats;
int shmid = -1;      // Shared memory ID
int semid = -1;      // Semaphore ID
volatile sig_atomic_t cleanup_done = 0;  // Prevent double cleanup

// Forward declarations
int create_semaphore(void);
void sem_wait_op(int semid);
void sem_signal_op(int semid);
void cleanup(int sig);
void check_worker_timeouts(void);
void send_slow_signal(pid_t pid);

// Semaphore wait operation (P)
void sem_wait_op(int semid) {
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op = -1;  // P operation (decrement)
    sb.sem_flg = 0;
    
    if (semop(semid, &sb, 1) == -1) {
        perror("sem_wait failed");
        exit(EXIT_FAILURE);
    }
}

// Semaphore signal operation (V)
void sem_signal_op(int semid) {
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op = 1;   // V operation (increment)
    sb.sem_flg = 0;
    
    if (semop(semid, &sb, 1) == -1) {
        perror("sem_signal failed");
        exit(EXIT_FAILURE);
    }
}

// Create and initialize binary semaphore
int create_semaphore(void) {
    int semid;
    union semun arg;
    
    // Create semaphore set with 1 semaphore
    semid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("semget failed");
        exit(EXIT_FAILURE);
    }
    
    // Initialize to 1 (unlocked/binary semaphore)
    arg.val = 1;
    if (semctl(semid, 0, SETVAL, arg) == -1) {
        perror("semctl SETVAL failed");
        exit(EXIT_FAILURE);
    }
    
    return semid;
}

// Send SIGUSR1 to slow worker (energy saving mode)
void send_slow_signal(pid_t pid) {
    if (pid > 0) {
        if (kill(pid, SIGUSR1) == -1) {
            if (errno != ESRCH) {  // Don't warn if process doesn't exist
                perror("kill SIGUSR1 failed");
            }
        }
    }
}

// Check for workers that haven't updated in WORKER_TIMEOUT seconds
void check_worker_timeouts(void) {
    time_t current_time = time(NULL);
    
    for (int i = 0; i < MAX_WORKERS; i++) {
        sem_wait_op(semid);
        
        time_t last_update = shared_stats[i].last_update;
        int is_slow = shared_stats[i].is_slow;
        pid_t pid = shared_stats[i].pid;
        
        // Check if worker is slow (no update for WORKER_TIMEOUT seconds)
        if (last_update > 0 && (current_time - last_update) > WORKER_TIMEOUT) {
            if (!is_slow) {
                // Worker just became slow
                shared_stats[i].is_slow = 1;
                printf("[⚠️  TIMEOUT] Worker %d (PID %d) is SLOW! "
                       "Last update: %ld seconds ago\n", 
                       i, pid, (long)(current_time - last_update));
                
                // Send signal to slow worker
                send_slow_signal(pid);
            }
        } else if (last_update > 0 && (current_time - last_update) <= WORKER_TIMEOUT) {
            // Worker recovered
            if (is_slow) {
                shared_stats[i].is_slow = 0;
                printf("[✅ RECOVERED] Worker %d (PID %d) is back to normal\n", i, pid);
            }
        }
        
        sem_signal_op(semid);
    }
}

// Signal handler for SIGUSR1 (worker slow notification)
void worker_signal_handler(int sig) {
    (void)sig;
    // Worker receives SIGUSR1 - enter energy saving mode
    printf("[WORKER] Received SIGUSR1 - Entering energy saving mode...\n");
    // In real implementation, worker would slow down its work rate
}

// Signal handler for cleanup
void cleanup(int sig) {
    (void)sig;  // Unused parameter (MISRA compliance)
    
    // Prevent double cleanup
    if (cleanup_done) {
        return;
    }
    cleanup_done = 1;
    
    printf("\n[SUPERVISEUR] Nettoyage et arrêt du système...\n");
    
    // Kill all workers first
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (shared_stats != NULL && shared_stats[i].pid > 0) {
            kill(shared_stats[i].pid, SIGTERM);
        }
    }
    
    // Wait briefly for workers to terminate
    usleep(100000);  // 100ms
    
    // Detach shared memory
    if (shared_stats != NULL && shared_stats != (void *)-1) {
        if (shmdt(shared_stats) == -1) {
            perror("shmdt failed");
        }
        shared_stats = NULL;
    }
    
    // Remove shared memory segment
    if (shmid != -1) {
        if (shmctl(shmid, IPC_RMID, NULL) == -1) {
            if (errno != EINVAL) {  // Don't warn if already deleted
                perror("shmctl failed");
            }
        }
        shmid = -1;  // Reset to prevent double cleanup
    }
    
    // Remove semaphore
    if (semid != -1) {
        if (semctl(semid, 0, IPC_RMID) == -1) {
            if (errno != EINVAL) {  // Don't warn if already deleted
                perror("semctl IPC_RMID failed");
            }
        }
        semid = -1;  // Reset to prevent double cleanup
    }
    
    printf("[SUPERVISEUR] Système arrêté proprement.\n");
    exit(EXIT_SUCCESS);
}

void worker_process(int id) {
    // Set up signal handler for SIGUSR1
    signal(SIGUSR1, worker_signal_handler);
    
    srand(time(NULL) ^ (getpid() << 16));
    printf("[WORKER %d] Démarré (PID: %d)\n", id, getpid());
    
    int energy_saving_mode = 0;
    
    while (1) {
        // Simulate work
        int work_time = (rand() % 3) + 1;
        
        // In energy saving mode, work slower
        if (energy_saving_mode) {
            work_time *= 2;  // Double the work time
            printf("[WORKER %d] Mode économie d'énergie (sleep %ds)\n", id, work_time);
        }
        
        sleep(work_time);
        
        // === PROTECTED WRITE with Semaphore ===
        sem_wait_op(semid);  // Lock (P operation)
        
        // Critical section: write to shared memory
        shared_stats[id].pid = getpid();
        shared_stats[id].tasks_completed++;
        shared_stats[id].health_score = rand() % 100;
        shared_stats[id].last_update = time(NULL);  // Update timestamp
        
        // Simulate random crash (5% chance)
        if (shared_stats[id].health_score < 5) {
            printf("[WORKER %d] Erreur critique ! Crash...\n", id);
            sem_signal_op(semid);  // Unlock before exit!
            exit(EXIT_FAILURE);
        }
        
        sem_signal_op(semid);  // Unlock (V operation)
        // === END PROTECTED WRITE ===
    }
}

int main(void) {
    pid_t pids[MAX_WORKERS];
    time_t last_timeout_check = 0;
    
    // Set up signal handlers
    if (signal(SIGINT, cleanup) == SIG_ERR) {
        perror("signal SIGINT failed");
        exit(EXIT_FAILURE);
    }
    
    if (signal(SIGUSR1, worker_signal_handler) == SIG_ERR) {
        perror("signal SIGUSR1 failed");
        exit(EXIT_FAILURE);
    }
    
    // 1. Create shared memory
    shmid = shmget(IPC_PRIVATE, sizeof(ProcessStat) * MAX_WORKERS, IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    
    // Attach shared memory
    shared_stats = (ProcessStat *)shmat(shmid, NULL, 0);
    if (shared_stats == (void *)-1) {
        perror("shmat failed");
        shmctl(shmid, IPC_RMID, NULL);  // Cleanup on failure
        shmid = -1;
        exit(EXIT_FAILURE);
    }
    
    // Initialize shared memory
    memset(shared_stats, 0, sizeof(ProcessStat) * MAX_WORKERS);
    
    // 2. Create semaphore
    semid = create_semaphore();
    
    // 3. Launch workers
    for (int i = 0; i < MAX_WORKERS; i++) {
        pids[i] = fork();
        if (pids[i] == -1) {
            perror("fork failed");
            cleanup(0);
        }
        
        if (pids[i] == 0) {
            // Child process
            worker_process(i);
            exit(EXIT_SUCCESS);  // Should never reach here
        }
        
        // Parent: initialize worker stats
        sem_wait_op(semid);
        shared_stats[i].pid = pids[i];
        shared_stats[i].last_update = time(NULL);
        shared_stats[i].is_slow = 0;
        sem_signal_op(semid);
    }
    
    // 4. Supervision loop (Parent process)
    printf("[SUPERVISEUR] Surveillance active...\n");
    printf("[SUPERVISEUR] Timeout threshold: %d seconds\n", WORKER_TIMEOUT);
    printf("[SUPERVISEUR] Refresh rate: %d seconds\n\n", REFRESH_RATE);
    
    while (1) {
        printf("\n========== Dashboard Temps Réel ==========\n");
        printf("Time: %s", ctime(&(time_t){time(NULL)}));
        printf("------------------------------------------\n");
        
        time_t current_time = time(NULL);
        
        for (int i = 0; i < MAX_WORKERS; i++) {
            // === PROTECTED READ with Semaphore ===
            sem_wait_op(semid);  // Lock
            
            // Critical section: read from shared memory
            time_t last_update = shared_stats[i].last_update;
            int is_slow = shared_stats[i].is_slow;
            time_t idle_time = (last_update > 0) ? (current_time - last_update) : 0;
            
            printf("Worker %d | PID: %d | Santé: %d%% | Tâches: %d | ",i,shared_stats[i].pid,shared_stats[i].health_score,shared_stats[i].tasks_completed);
            
            if (is_slow) {
                printf("⚠️  SLOW (%lds idle)", (long)idle_time);
            } else if (idle_time > WORKER_TIMEOUT / 2) {
                printf("⏱  (%lds idle)", (long)idle_time);
            } else {
                printf("✓ OK");
            }
            printf("\n");
            
            sem_signal_op(semid);  // Unlock
            // === END PROTECTED READ ===
            
            // Check if worker is still alive
            int status;
            pid_t result = waitpid(pids[i], &status, WNOHANG);
            
            if (result == -1) {
                // Error occurred
                perror("waitpid failed");
            } else if (result == pids[i]) {
                // Worker terminated
                if (WIFEXITED(status)) {
                    printf("[ALERTE] Worker %d exited with code %d. Relance...\n",i, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    printf("[ALERTE] Worker %d killed by signal %d. Relance...\n", i, WTERMSIG(status));
                }
                
                // Restart worker
                pids[i] = fork();
                if (pids[i] == -1) {
                    perror("fork failed during restart");
                } else if (pids[i] == 0) {
                    worker_process(i);
                    exit(EXIT_SUCCESS);
                }
                
                // Update shared stats for new worker
                sem_wait_op(semid);
                shared_stats[i].pid = pids[i];
                shared_stats[i].last_update = time(NULL);
                shared_stats[i].is_slow = 0;
                sem_signal_op(semid);
            }
        }
        
        // Check for slow workers periodically (every REFRESH_RATE seconds)
        if ((current_time - last_timeout_check) >= REFRESH_RATE) {
            check_worker_timeouts();
            last_timeout_check = current_time;
        }
        
        printf("==========================================\n");
        sleep(REFRESH_RATE);  // Refresh rate
    }
    
    return 0;
}