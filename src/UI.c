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
// Global ncurses windows
static WINDOW *main_win;
static WINDOW *header_win;
static WINDOW *workers_win;
static WINDOW *logs_win;

// Screen dimensions
#define HEADER_HEIGHT 3
#define LOGS_HEIGHT 8
#define TABLE_COL_WIDTH 15

// Latest log message buffer
static char latest_log[256] = "[SYSTEM] Dashboard initialized";

// Circular buffer for logs (last 100 messages)
#define MAX_LOGS 100
static char log_buffer[MAX_LOGS][256];
static int log_index = 0;

int ui_init(void) {
    // Initialize ncurses
    initscr();
    cbreak();              // Disable line buffering
    noecho();              // Don't echo keystrokes
    curs_set(0);           // Hide cursor
    nodelay(stdscr, TRUE); // Non-blocking input
    
    // Start colors
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_GREEN, COLOR_BLACK);   // OK status
        init_pair(2, COLOR_YELLOW, COLOR_BLACK);  // Warning
        init_pair(3, COLOR_RED, COLOR_BLACK);     // Error/Critical
        init_pair(4, COLOR_CYAN, COLOR_BLACK);    // Info
        init_pair(5, COLOR_WHITE, COLOR_BLUE);    // Header
    }
    
    // Create main window
    main_win = newwin(LINES, COLS, 0, 0);
    keypad(main_win, TRUE);
    
    // Calculate dimensions
    int workers_height = LINES - HEADER_HEIGHT - LOGS_HEIGHT - 2;
    
    // Create header window (top)
    header_win = newwin(HEADER_HEIGHT, COLS - 2, 1, 1);
    box(header_win, 0, 0);
    
    // Create workers table window (middle)
    workers_win = newwin(workers_height, COLS - 2, HEADER_HEIGHT + 1, 1);
    box(workers_win, 0, 0);
    
    // Create logs window (bottom)
    logs_win = newwin(LOGS_HEIGHT, COLS - 2, LINES - LOGS_HEIGHT - 1, 1);
    box(logs_win, 0, 0);
    
    // Create title
    mvwprintw(main_win, 0, (COLS - 30) / 2, " RT-THM MONITORING SYSTEM ");
    wattron(main_win, COLOR_PAIR(5) | A_BOLD);
    mvwprintw(main_win, 0, (COLS - 30) / 2, " RT-THM MONITORING SYSTEM ");
    wattroff(main_win, COLOR_PAIR(5) | A_BOLD);
    
    refresh();
    
    return 0;
}

void ui_cleanup(void) {
    // Restore terminal to normal mode
    endwin();
    
    // Clear all windows
    if (main_win) delwin(main_win);
    if (header_win) delwin(header_win);
    if (workers_win) delwin(workers_win);
    if (logs_win) delwin(logs_win);
    
    // Reset ncurses
    curs_set(1);  // Show cursor again
    nocbreak();
    echo();
    
    // Clear screen
    clear();
    refresh();
}

const char* ui_get_latest_log(void) {
    return latest_log;
}
void ui_add_log(const char *format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Add to circular buffer
    strncpy(log_buffer[log_index], buffer, 255);
    log_buffer[log_index][255] = '\0';
    log_index = (log_index + 1) % MAX_LOGS;
    strncpy(latest_log, buffer, 255);
}

void ui_draw_system_info(void) {
    // Get system time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Calculate total tasks
    int total_tasks = 0;
    int active_workers = 0;
    int critical_workers = 0;
    
    if (shared_stats) {
        for (int i = 0; i < MAX_WORKERS; i++) {
            total_tasks += shared_stats[i].tasks_completed;
            if (shared_stats[i].pid > 0) active_workers++;
            if (shared_stats[i].health_score < 20) critical_workers++;
        }
    }
    
    // Draw header
    werase(header_win);
    box(header_win, 0, 0);
    
    // Line 1: Title and time
    wattron(header_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(header_win, 1, 2, "SYSTEM STATUS");
    wattroff(header_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(header_win, 1, COLS - 25, "%s", time_str);
    
    // Line 2: Statistics
    wattron(header_win, COLOR_PAIR(1));
    mvwprintw(header_win, 2, 2, "Workers: %d/%d active", active_workers, MAX_WORKERS);
    wattroff(header_win, COLOR_PAIR(1));
    
    if (critical_workers > 0) {
        wattron(header_win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(header_win, 2, 30, "⚠️  %d CRITICAL", critical_workers);
        wattroff(header_win, COLOR_PAIR(3) | A_BOLD);
    }
    
    mvwprintw(header_win, 2, 55, "Total Tasks: %d", total_tasks);
    mvwprintw(header_win, 2, 80, "Timeout: %ds", WORKER_TIMEOUT);
    
    wnoutrefresh(header_win);
}

void ui_draw_workers_table(void) {
    werase(workers_win);
    box(workers_win, 0, 0);
    
    // Table header
    wattron(workers_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(workers_win, 1, 2, "ID");
    mvwprintw(workers_win, 1, 10, "PID");
    mvwprintw(workers_win, 1, 22, "Health");
    mvwprintw(workers_win, 1, 35, "Tasks");
    mvwprintw(workers_win, 1, 48, "Status");
    mvwprintw(workers_win, 1, 62, "Last Update");
    wattroff(workers_win, A_BOLD | COLOR_PAIR(4));
    
    // Horizontal line
    for (int i = 1; i < COLS - 3; i++) {
        mvwaddch(workers_win, 2, i, ACS_HLINE);
    }
    
    // Worker rows
    if (shared_stats) {
        time_t current_time = time(NULL);
        
        for (int i = 0; i < MAX_WORKERS && i < LINES - HEADER_HEIGHT - LOGS_HEIGHT - 5; i++) {
            int row = i + 3;
            
            // Get worker data
            sem_wait_op(semid);
            pid_t pid = shared_stats[i].pid;
            int health = shared_stats[i].health_score;
            int tasks = shared_stats[i].tasks_completed;
            time_t last_update = shared_stats[i].last_update;
            int is_slow = shared_stats[i].is_slow;
            sem_signal_op(semid);
            
            // Calculate idle time
            time_t idle = (last_update > 0) ? (current_time - last_update) : 0;
            char last_update_str[32];
            
            if (last_update == 0) {
                strncpy(last_update_str, "Never", sizeof(last_update_str));
            } else {
                struct tm *tm = localtime(&last_update);
                strftime(last_update_str, sizeof(last_update_str), "%H:%M:%S", tm);
            }
            
            // Determine status and color
            if (pid <= 0) {
                wattron(workers_win, COLOR_PAIR(3));
                mvwprintw(workers_win, row, 2, "%d", i);
                mvwprintw(workers_win, row, 10, "N/A");
                mvwprintw(workers_win, row, 22, "---");
                mvwprintw(workers_win, row, 35, "0");
                mvwprintw(workers_win, row, 48, "DEAD");
                wattroff(workers_win, COLOR_PAIR(3));
            } else {
                mvwprintw(workers_win, row, 2, "%d", i);
                mvwprintw(workers_win, row, 10, "%d", pid);
                
                // Health with color
                if (health < 20) {
                    wattron(workers_win, COLOR_PAIR(3) | A_BOLD);
                    mvwprintw(workers_win, row, 22, "%3d%% CRITICAL", health);
                    wattroff(workers_win, COLOR_PAIR(3) | A_BOLD);
                } else if (health < 50) {
                    wattron(workers_win, COLOR_PAIR(2));
                    mvwprintw(workers_win, row, 22, "%3d%% WARNING", health);
                    wattroff(workers_win, COLOR_PAIR(2));
                } else {
                    wattron(workers_win, COLOR_PAIR(1));
                    mvwprintw(workers_win, row, 22, "%3d%% OK", health);
                    wattroff(workers_win, COLOR_PAIR(1));
                }
                
                mvwprintw(workers_win, row, 35, "%d", tasks);
                
                // Status
                if (is_slow) {
                    wattron(workers_win, COLOR_PAIR(2) | A_BOLD);
                    mvwprintw(workers_win, row, 48, "⚠️  SLOW (%lds)", (long)idle);
                    wattroff(workers_win, COLOR_PAIR(2) | A_BOLD);
                } else if (idle > WORKER_TIMEOUT / 2) {
                    wattron(workers_win, COLOR_PAIR(4));
                    mvwprintw(workers_win, row, 48, "⏱  %lds idle", (long)idle);
                    wattroff(workers_win, COLOR_PAIR(4));
                } else {
                    wattron(workers_win, COLOR_PAIR(1));
                    mvwprintw(workers_win, row, 48, "✓ RUNNING");
                    wattroff(workers_win, COLOR_PAIR(1));
                }
            }
            
            // Last update time
            mvwprintw(workers_win, row, 62, "%s", last_update_str);
        }
    }
    
    wnoutrefresh(workers_win);
}

void ui_draw_logs_console(void) {
    werase(logs_win);
    box(logs_win, 0, 0);
    
    // Title
    wattron(logs_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(logs_win, 0, 2, " LIVE LOGS ");
    wattroff(logs_win, A_BOLD | COLOR_PAIR(4));
    
    // Display last N log messages
    int start_idx = (log_index >= LOGS_HEIGHT - 2) ? 
                    (log_index - (LOGS_HEIGHT - 2)) : 0;
    int count = 0;
    
    for (int i = start_idx; i < log_index && count < LOGS_HEIGHT - 2; i++, count++) {
        wattron(logs_win, COLOR_PAIR(4));
        mvwprintw(logs_win, count + 1, 2, "%s", log_buffer[i]);
        wattroff(logs_win, COLOR_PAIR(4));
    }
    
    wnoutrefresh(logs_win);
}

void ui_draw_dashboard(void) {
    ui_draw_system_info();
    ui_draw_workers_table();
    ui_draw_logs_console();
    doupdate();  // Update all windows at once
}

void ui_refresh(void) {
    ui_draw_dashboard();
    usleep(50000);  // 50ms refresh rate
}