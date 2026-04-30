#include "project.h"
#include "config.h"
#include "logger.h"

// Runtime configuration variables
int MAX_WORKERS = 3;
int WORKER_TIMEOUT = 10;
int REFRESH_RATE = 2;
int LOG_LEVEL = LOG_INFO;

int load_config(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "[CONFIG] Warning: %s not found, using defaults\n", filename);
        log_event("WARN", "Config file %s not found, using defaults", filename);
        return -1;
    }
    
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        
        // Remove newline
        line[strcspn(line, "\r\n")] = '\0';
        
        // Parse key=value
        char *key = strtok(line, "=");
        char *value = strtok(NULL, "=");
        if (!key || !value) continue;
        
        // Trim whitespace
        while (isspace((unsigned char)*key)) key++;
        while (isspace((unsigned char)*value)) value++;
        
        int int_val = atoi(value);
        
        if (strcmp(key, "workers") == 0) {
            if (int_val > 0 && int_val <= 10) {
                MAX_WORKERS = int_val;
                log_event("INFO", "Config: workers = %d", MAX_WORKERS);
            }
        } else if (strcmp(key, "timeout") == 0) {
            if (int_val > 0) {
                WORKER_TIMEOUT = int_val;
                log_event("INFO", "Config: timeout = %ds", WORKER_TIMEOUT);
            }
        } else if (strcmp(key, "refresh_rate") == 0) {
            if (int_val > 0) {
                REFRESH_RATE = int_val;
                log_event("INFO", "Config: refresh_rate = %ds", REFRESH_RATE);
            }
        } else if (strcmp(key, "log_level") == 0) {
            if (int_val >= LOG_NONE && int_val <= LOG_DEBUG) {
                LOG_LEVEL = int_val;
                log_event("INFO", "Config: log_level = %d", LOG_LEVEL);
            }
        }
    }
    
    fclose(fp);
    return 0;
}

void print_config(void) {
    printf("[CONFIG] workers=%d, timeout=%ds, refresh=%ds, log_level=%d\n",MAX_WORKERS, WORKER_TIMEOUT, REFRESH_RATE, LOG_LEVEL);
}

int validate_config(void) {
    if (MAX_WORKERS < 1 || MAX_WORKERS > 10) {
        log_event("ERROR", "Invalid workers count: %d", MAX_WORKERS);
        return -1;
    }
    if (WORKER_TIMEOUT < 1) {
        log_event("ERROR", "Invalid timeout: %d", WORKER_TIMEOUT);
        return -1;
    }
    if (REFRESH_RATE < 1) {
        log_event("ERROR", "Invalid refresh_rate: %d", REFRESH_RATE);
        return -1;
    }
    return 0;
}