/**
 * @file ui.h
 * @brief Ncurses-based dashboard for RT-THM monitoring
 * @details Provides real-time visual interface with system info, worker table, and live logs.
 */

#ifndef UI_H
#define UI_H

#include "project.h"
#include <ncurses.h>
#include <stdarg.h>

/**
 * @brief Initialize ncurses and create dashboard windows
 * @return 0 on success, -1 if terminal too small or init fails
 * @note Call once at startup before any drawing functions
 */
int ui_init(void);

/**
 * @brief Cleanup ncurses and restore terminal to normal mode
 * @note Must be called before exit to prevent terminal corruption
 */
void ui_cleanup(void);

/**
 * @brief Draw complete dashboard (header + workers + logs)
 * @note Uses double-buffering via doupdate() for flicker-free refresh
 */
void ui_draw_dashboard(void);

/**
 * @brief Draw system status header (time, workers, tasks)
 * @note Reads shared_stats atomically via semaphore
 */
void ui_draw_system_info(void);

/**
 * @brief Draw worker statistics table with color-coded status
 * @note Each row shows: ID, PID, Health, Tasks, Status, Last Update
 */
void ui_draw_workers_table(void);

/**
 * @brief Draw live log console at bottom of screen
 * @note Displays last N messages from circular log buffer
 */
void ui_draw_logs_console(void);

/**
 * @brief Add formatted log message to UI console buffer
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 * @note Thread-safe via mutex; truncates long messages to prevent wrap
 */
void ui_add_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Get most recent log message
 * @return Pointer to latest log string (do not modify)
 */
const char* ui_get_latest_log(void);

/**
 * @brief Refresh entire dashboard display
 * @note Calls doupdate() for atomic screen update
 */
void ui_refresh(void);

/**
 * @brief Handle terminal resize event
 * @note Recreates all windows with new dimensions; call on KEY_RESIZE
 */
void ui_handle_resize(void);

#endif // UI_H