/**
 * @file config.h
 * @brief Configuration parser for RT-THM
 * @details Loads runtime parameters from config.txt without recompilation.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "project.h"

/**
 * @brief Load configuration from external file
 * @param filename Path to config file (e.g., "config.txt")
 * @return 0 on success, -1 if file not found (defaults used)
 * @note Modifies global vars: MAX_WORKERS, WORKER_TIMEOUT, REFRESH_RATE, LOG_LEVEL
 */
int load_config(const char *filename);

/**
 * @brief Print current config values to stdout
 * @details Format: [CONFIG] workers=4, timeout=10s, refresh=2s, log_level=2
 */
void print_config(void);

/**
 * @brief Validate config values against constraints
 * @return 0 if valid, -1 if out-of-range value detected
 * @note Call after load_config() before main loop
 */
int validate_config(void);

#endif // CONFIG_H