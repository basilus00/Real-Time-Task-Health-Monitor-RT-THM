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

// ============ CONSTANTS ============
#define SHM_SIZE 1024
#define CONFIG_FILE "config.txt"
#define LOG_FILE "system.log"
#define MAX_LINE 256
#define CMD_NONE   0
#define CMD_PAUSE  1
#define CMD_RESUME 2
#define LOG_NONE  0
#define LOG_INFO  1
#define LOG_DEBUG 2

// ============ STRUCTURES ============
typedef struct {
    int pid;
    int health_score;
    int tasks_completed;
    time_t last_update;
    int is_slow;
    int command;
} ProcessStat;

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// ============ EXTERN DECLARATIONS (No memory allocated here!) ============
extern ProcessStat *shared_stats;
extern int shmid;
extern int semid;
extern volatile sig_atomic_t cleanup_done;
extern FILE *log_fp;

// Runtime configuration (extern - defined in config.c)
extern int MAX_WORKERS;
extern int WORKER_TIMEOUT;
extern int REFRESH_RATE;
extern int LOG_LEVEL;

#endif // PROJECT_H