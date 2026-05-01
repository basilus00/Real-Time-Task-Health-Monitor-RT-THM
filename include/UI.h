#ifndef UI_H
#define UI_H

#include "project.h"
#include <ncurses.h>
#include <stdarg.h>

// UI initialization
int ui_init(void);
void ui_cleanup(void);

// Dashboard drawing
void ui_draw_dashboard(void);
void ui_draw_system_info(void);
void ui_draw_workers_table(void);
void ui_draw_logs_console(void);

// Logging to UI console (variadic, like printf)
void ui_add_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
const char* ui_get_latest_log(void);

// Refresh and resize handling
void ui_refresh(void);
void ui_handle_resize(void);

#endif // UI_H