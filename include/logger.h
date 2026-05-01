/**
 * @file logger.h
 * @brief Logging system for RT-THM with microsecond precision
 * @details Supports multiple log levels and thread-safe file output.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include "project.h"

/**
 * @brief Write formatted log event to file and optionally console
 * @param level Log level string ("INFO", "WARN", "ERROR", "DEBUG")
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 * @note Thread-safe via file append mode; respects LOG_LEVEL global
 */
void log_event(const char *level, const char *format, ...);

/**
 * @brief Initialize logging system and open log file
 * @return 0 on success, -1 if log file cannot be opened
 * @note Call once at program startup before any log_event() calls
 */
int logger_init(void);

/**
 * @brief Close log file and cleanup resources
 * @note Call during shutdown to ensure all logs are flushed
 */
void logger_close(void);

#endif // LOGGER_H