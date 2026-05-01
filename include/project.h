/**
 * @file project.h
 * @brief Main header for RT-THM (Real-Time Task & Health Monitor)
 * @details Core definitions: structures, constants, and extern declarations
 * shared across all modules.
 */

#ifndef PROJECT_H
#define PROJECT_H

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
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <ncurses.h>

/** @name Configuration Constants */
/**@{*/
#define SHM_SIZE 1024           ///< Shared memory segment size in bytes
#define CONFIG_FILE "config.txt" ///< Runtime config file path
#define LOG_FILE "system.log"    ///< Log output file path
#define MAX_LINE 256             ///< Max line length for config parsing
/**@}*/

/** @name Command Constants (Bidirectional IPC) */
/**@{*/
#define CMD_NONE   0  ///< No command pending
#define CMD_PAUSE  1  ///< Pause worker execution
#define CMD_RESUME 2  ///< Resume worker execution
/**@}*/

/** @name Log Level Constants */
/**@{*/
#define LOG_NONE  0  ///< Disable logging
#define LOG_INFO  1  ///< Info and error messages
#define LOG_DEBUG 2  ///< Verbose debug output
/**@}*/

/**
 * @brief Process status structure for shared memory IPC
 * @details Each worker updates its own entry; parent reads for dashboard.
 * All accesses must be protected by semaphore.
 */
typedef struct {
    int pid;                ///< Worker process ID
    int health_score;       ///< Health percentage (0-100)
    int tasks_completed;    ///< Total tasks finished
    time_t last_update;     ///< Last stats update timestamp
    int is_slow;            ///< Slow-mode flag (0=normal, 1=slow)
    int command;            ///< Command from parent (CMD_*)
} ProcessStat;

/**
 * @brief Union for semctl() - required by POSIX
 * @note POSIX does not define this union; must be declared by application
 */
union semun {
    int val;                    ///< Value for SETVAL
    struct semid_ds *buf;       ///< Buffer for IPC_STAT/SET
    unsigned short *array;      ///< Array for GETALL/SETALL
};

/** @name Global Variables (Defined in config.c / main.c) */
/**@{*/
extern ProcessStat *shared_stats;       ///< Pointer to shared memory segment
extern int shmid;                        ///< Shared memory segment ID
extern int semid;                        ///< Semaphore set ID
extern volatile sig_atomic_t cleanup_done; ///< Shutdown flag (signal-safe)
extern FILE *log_fp;                     ///< Log file pointer
extern int MAX_WORKERS;                  ///< Number of worker processes
extern int WORKER_TIMEOUT;               ///< Timeout threshold (seconds)
extern int REFRESH_RATE;                 ///< Dashboard refresh interval (seconds)
extern int LOG_LEVEL;                    ///< Current logging verbosity
/**@}*/

#endif // PROJECT_H