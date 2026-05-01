/**
 * @file UI.c
 * @brief Ncurses dashboard implementation for RT-THM
 * @details Provides real-time visual interface with system info, worker table, and live logs.
 * Uses double-buffering and atomic snapshots for flicker-free, thread-safe rendering.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "project.h"
#include "ipc.h"
#include "config.h"
#include "ui.h"

#include <unistd.h>
#include <stdarg.h>
#include <ncurses.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>

/** @name Global Ncurses Windows (static to this module) */
/**@{*/
static WINDOW *main_win = NULL;      ///< Main screen window
static WINDOW *header_win = NULL;    ///< Top status bar window
static WINDOW *workers_win = NULL;   ///< Worker table window
static WINDOW *logs_win = NULL;      ///< Live logs console window
/**@}*/

/** @name Screen Layout Constants */
/**@{*/
#define HEADER_HEIGHT 3    ///< Height of header window (lines)
#define LOGS_HEIGHT 8      ///< Height of logs window (lines)
#define MIN_COLS 80        ///< Minimum terminal width
#define MIN_LINES 24       ///< Minimum terminal height
/**@}*/

/** @name Log Buffer (circular, thread-safe) */
/**@{*/
#define MAX_LOGS 100
static char log_buffer[MAX_LOGS][256];  ///< Circular buffer for log messages
static int log_index = 0;                ///< Current write index
static char latest_log[256] = "[SYSTEM] Dashboard initialized";  ///< Most recent log
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;    ///< Mutex for log access
/**@}*/

/**
 * @brief Initialize ncurses and create dashboard windows
 * @return 0 on success, -1 if terminal too small or init fails
 * @note Sets up colors, key handling, and window layout; call once at startup
 */
int ui_init(void) {
    initscr();
    
    if (COLS < MIN_COLS || LINES < MIN_LINES) {
        endwin();
        fprintf(stderr, "[UI] Terminal too small: %dx%d (min: %dx%d)\n", 
                COLS, LINES, MIN_COLS, MIN_LINES);
        return -1;
    }
    
    cbreak();
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN, -1);
        init_pair(2, COLOR_YELLOW, -1);
        init_pair(3, COLOR_RED, -1);
        init_pair(4, COLOR_CYAN, -1);
        init_pair(5, COLOR_WHITE, COLOR_BLUE);
    }
    
    main_win = newwin(LINES, COLS, 0, 0);
    if (!main_win) { endwin(); return -1; }
    
    int workers_height = LINES - HEADER_HEIGHT - LOGS_HEIGHT - 2;
    if (workers_height < 5) workers_height = 5;
    
    header_win = newwin(HEADER_HEIGHT, COLS - 2, 1, 1);
    if (header_win) box(header_win, 0, 0);
    
    workers_win = newwin(workers_height, COLS - 2, HEADER_HEIGHT + 1, 1);
    if (workers_win) box(workers_win, 0, 0);
    
    logs_win = newwin(LOGS_HEIGHT, COLS - 2, LINES - LOGS_HEIGHT - 1, 1);
    if (logs_win) box(logs_win, 0, 0);
    
    if (main_win && COLS >= 30) {
        int title_pos = (COLS - 30) / 2;
        if (title_pos < 1) title_pos = 1;
        wattron(main_win, COLOR_PAIR(5) | A_BOLD);
        mvwprintw(main_win, 0, title_pos, " RT-THM MONITORING SYSTEM ");
        wattroff(main_win, COLOR_PAIR(5) | A_BOLD);
    }
    
    refresh();
    return 0;
}

/**
 * @brief Cleanup ncurses and restore terminal to normal mode
 * @note Must be called before exit to prevent terminal corruption
 */
void ui_cleanup(void) {
    endwin();
    
    if (main_win) { delwin(main_win); main_win = NULL; }
    if (header_win) { delwin(header_win); header_win = NULL; }
    if (workers_win) { delwin(workers_win); workers_win = NULL; }
    if (logs_win) { delwin(logs_win); logs_win = NULL; }
    
    curs_set(1);
    nocbreak();
    echo();
    nodelay(stdscr, FALSE);
    
    clear();
    refresh();
}

/**
 * @brief Get most recent log message
 * @return Pointer to latest log string (do not modify)
 */
const char* ui_get_latest_log(void) {
    return latest_log;
}

/**
 * @brief Add formatted log message to UI console buffer
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 * @note Thread-safe via mutex; truncates long messages to prevent wrap
 */
void ui_add_log(const char *format, ...) {
    char buffer[256];
    
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = '\0';
    
    pthread_mutex_lock(&log_mutex);
    
    strncpy(log_buffer[log_index], buffer, 255);
    log_buffer[log_index][255] = '\0';
    log_index = (log_index + 1) % MAX_LOGS;
    strncpy(latest_log, buffer, 255);
    latest_log[255] = '\0';
    
    pthread_mutex_unlock(&log_mutex);
}

/**
 * @brief Draw system status header (time, workers, tasks)
 * @note Reads shared_stats atomically via semaphore; bounds-checks all output
 */
void ui_draw_system_info(void) {
    if (!header_win) return;
    
    werase(header_win);
    
    int total_tasks = 0, active_workers = 0, critical_workers = 0;
    if (shared_stats && semid != -1) {
        sem_wait_op(semid);
        for (int i = 0; i < MAX_WORKERS; i++) {
            total_tasks += shared_stats[i].tasks_completed;
            if (shared_stats[i].pid > 0) active_workers++;
            if (shared_stats[i].health_score < 20) critical_workers++;
        }
        sem_signal_op(semid);
    }
    
    box(header_win, 0, 0);
    
    int max_col = COLS - 2;
    if (max_col < 10) max_col = 10;
    
    wattron(header_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(header_win, 1, 2, "SYSTEM STATUS");
    wattroff(header_win, A_BOLD | COLOR_PAIR(4));
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    if (max_col > 20) {
        mvwprintw(header_win, 1, max_col - 15, "%s", time_str);
    }
    
    wattron(header_win, COLOR_PAIR(1));
    mvwprintw(header_win, 2, 2, "Workers: %d/%d", active_workers, MAX_WORKERS);
    wattroff(header_win, COLOR_PAIR(1));
    
    if (critical_workers > 0 && max_col > 30) {
        wattron(header_win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(header_win, 2, 20, "[!] %d CRIT", critical_workers);
        wattroff(header_win, COLOR_PAIR(3) | A_BOLD);
    }
    
    if (max_col > 50) {
        mvwprintw(header_win, 2, 35, "Tasks: %d", total_tasks);
    }
    
    wnoutrefresh(header_win);
}

/**
 * @brief Draw worker statistics table with color-coded status
 * @note Uses atomic snapshot of shared_stats to minimize semaphore hold time
 */
void ui_draw_workers_table(void) {
    if (!workers_win || !shared_stats || semid == -1) return;
    
    werase(workers_win);
    box(workers_win, 0, 0);
    
    typedef struct {
        int pid;
        int health;
        int tasks;
        int is_slow;
        time_t last_update;
    } WorkerSnap;
    
    WorkerSnap snaps[MAX_WORKERS];
    
    sem_wait_op(semid);
    for (int i = 0; i < MAX_WORKERS; i++) {
        snaps[i].pid = shared_stats[i].pid;
        snaps[i].health = shared_stats[i].health_score;
        snaps[i].tasks = shared_stats[i].tasks_completed;
        snaps[i].is_slow = shared_stats[i].is_slow;
        snaps[i].last_update = shared_stats[i].last_update;
    }
    sem_signal_op(semid);
    
    wattron(workers_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(workers_win, 1, 1, "%-3s %-6s %-8s %-6s %-8s", 
              "ID", "PID", "Health", "Tasks", "Status");
    wattroff(workers_win, A_BOLD | COLOR_PAIR(4));
    
    int width = COLS - 4;
    if (width > 2 && width < 200) {
        for (int i = 1; i < width; i++) {
            mvwaddch(workers_win, 2, i, ACS_HLINE);
        }
    }
    
    int max_rows = LINES - HEADER_HEIGHT - LOGS_HEIGHT - 6;
    if (max_rows < 1) max_rows = 1;
    
    for (int i = 0; i < MAX_WORKERS && i < max_rows; i++) {
        int row = i + 3;
        
        const char *status;
        int color_pair;
        
        if (snaps[i].pid <= 0) {
            status = "DEAD";
            color_pair = 3;
        } else if (snaps[i].health < 20) {
            status = "CRIT";
            color_pair = 3;
        } else if (snaps[i].health < 50) {
            status = "WARN";
            color_pair = 2;
        } else if (snaps[i].is_slow) {
            status = "SLOW";
            color_pair = 2;
        } else {
            status = "OK";
            color_pair = 1;
        }
        
        if (color_pair == 3) {
            wattron(workers_win, COLOR_PAIR(3) | A_BOLD);
        } else if (color_pair == 2) {
            wattron(workers_win, COLOR_PAIR(2));
        } else {
            wattron(workers_win, COLOR_PAIR(1));
        }
        
        mvwprintw(workers_win, row, 1, "%-3d %-6d %-8d %-6d %-8s",
                  i, snaps[i].pid, snaps[i].health, snaps[i].tasks, status);
        
        if (color_pair != 0) {
            wattroff(workers_win, COLOR_PAIR(color_pair));
            if (color_pair == 3 || color_pair == 2) {
                wattroff(workers_win, A_BOLD);
            }
        }
    }
    
    wnoutrefresh(workers_win);
}

/**
 * @brief Draw live log console at bottom of screen
 * @note Thread-safe read via mutex; truncates messages to prevent line wrap
 */
void ui_draw_logs_console(void) {
    if (!logs_win) return;
    
    werase(logs_win);
    box(logs_win, 0, 0);
    
    wattron(logs_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(logs_win, 0, 2, " LIVE LOGS ");
    wattroff(logs_win, A_BOLD | COLOR_PAIR(4));
    
    pthread_mutex_lock(&log_mutex);
    
    int visible_lines = LOGS_HEIGHT - 2;
    int start = (log_index >= visible_lines) ? (log_index - visible_lines) : 0;
    
    for (int i = start, line = 1; i < log_index && line < LOGS_HEIGHT - 1; i++, line++) {
        char msg[60];
        size_t len = strlen(log_buffer[i]);
        if (len >= sizeof(msg)) {
            strncpy(msg, log_buffer[i], sizeof(msg) - 4);
            strcpy(msg + sizeof(msg) - 4, "...");
        } else {
            strncpy(msg, log_buffer[i], sizeof(msg));
        }
        msg[sizeof(msg) - 1] = '\0';
        
        wattron(logs_win, COLOR_PAIR(4));
        mvwprintw(logs_win, line, 2, "%-58s", msg);
        wattroff(logs_win, COLOR_PAIR(4));
    }
    
    pthread_mutex_unlock(&log_mutex);
    
    wnoutrefresh(logs_win);
}

/**
 * @brief Draw complete dashboard (header + workers + logs)
 * @note Uses doupdate() for atomic, flicker-free screen refresh
 */
void ui_draw_dashboard(void) {
    ui_draw_system_info();
    ui_draw_workers_table();
    ui_draw_logs_console();
    doupdate();
}

/**
 * @brief Refresh entire dashboard display
 * @note Clears stdscr first to prevent ghosting; includes small delay
 */
void ui_refresh(void) {
    clear();
    refresh();
    ui_draw_dashboard();
    usleep(50000);
}

/**
 * @brief Handle terminal resize event
 * @note Recreates all windows with new dimensions; call on KEY_RESIZE
 */
void ui_handle_resize(void) {
    if (header_win) delwin(header_win);
    if (workers_win) delwin(workers_win);
    if (logs_win) delwin(logs_win);
    
    endwin();
    refresh();
    
    int workers_h = LINES - HEADER_HEIGHT - LOGS_HEIGHT - 2;
    if (workers_h < 5) workers_h = 5;
    
    header_win = newwin(HEADER_HEIGHT, COLS - 2, 1, 1);
    workers_win = newwin(workers_h, COLS - 2, HEADER_HEIGHT + 1, 1);
    logs_win = newwin(LOGS_HEIGHT, COLS - 2, LINES - LOGS_HEIGHT - 1, 1);
    
    if (header_win) box(header_win, 0, 0);
    if (workers_win) box(workers_win, 0, 0);
    if (logs_win) box(logs_win, 0, 0);
    
    touchwin(header_win);
    touchwin(workers_win);
    touchwin(logs_win);
    clearok(stdscr, TRUE);
}