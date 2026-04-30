#ifndef LOGGER_H
#define LOGGER_H

#include "project.h"

// Logging function with variable arguments
void log_event(const char *level, const char *format, ...);

// Initialize/close log file
int logger_init(void);
void logger_close(void);

#endif // LOGGER_H