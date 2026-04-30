#ifndef SIGNALS_H
#define SIGNALS_H

#include "project.h"

// Signal handlers
void cleanup(int sig);
void worker_signal_handler(int sig);

// Send signal to worker
void send_signal_to_worker(pid_t pid, int sig_num);

// Register all signal handlers
int register_signal_handlers(void);

#endif // SIGNALS_H