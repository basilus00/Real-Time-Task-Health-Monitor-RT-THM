#include "project.h"
#include "logger.h"

int logger_init(void) {
    log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) {
        fprintf(stderr, "[LOGGER] Failed to open %s\n", LOG_FILE);
        return -1;
    }
    return 0;
}

void logger_close(void) {
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
}

void log_event(const char *level, const char *format, ...) {
    if (LOG_LEVEL == LOG_NONE) return;
    
    // Get precise timestamp with microseconds
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime(&tv.tv_sec);
    
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Open log file in append mode for each write (safe for fork)
    FILE *fp = fopen(LOG_FILE, "a");
    if (!fp) return;
    
    // Write header
    fprintf(fp, "[%s.%06ld][%s][PID:%d] ", 
            timestamp, (long)tv.tv_usec, level, getpid());
    
    // Write message
    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);
    fprintf(fp, "\n");
    fclose(fp);
    
    // Also print to console if DEBUG level
    if (LOG_LEVEL >= LOG_DEBUG) {
        printf("[LOG] %s.%06ld %s: ", timestamp, (long)tv.tv_usec, level);
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        printf("\n");
    }
}