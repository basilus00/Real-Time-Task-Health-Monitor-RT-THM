#ifndef CONFIG_H
#define CONFIG_H

#include "project.h"

// Load configuration from file
int load_config(const char *filename);

// Print current config to stdout
void print_config(void);

// Validate config values
int validate_config(void);

#endif // CONFIG_H