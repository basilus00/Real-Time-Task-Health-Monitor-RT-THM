#ifndef UI_H
#define UI_H

#include "project.h"

// Initialize ncurses
int ui_init(void);

// Cleanup ncurses
void ui_cleanup(void);

// Draw the complete dashboard
void ui_draw_dashboard(void);

// Draw system info (top section)
void ui_draw_system_info(void);

// Draw workers table (middle section)
void ui_draw_workers_table(void);

// Draw logs console (bottom section)
void ui_draw_logs_console(const char *log_message);

// Refresh display
void ui_refresh(void);

// Get log message to display
const char* ui_get_latest_log(void);

#endif // UI_H