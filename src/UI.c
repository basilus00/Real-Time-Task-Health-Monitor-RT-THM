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

// ============ GLOBAL NCURSES WINDOWS ============
static WINDOW *main_win = NULL;
static WINDOW *header_win = NULL;
static WINDOW *workers_win = NULL;
static WINDOW *logs_win = NULL;

// ============ SCREEN DIMENSIONS ============
#define HEADER_HEIGHT 3
#define LOGS_HEIGHT 8
#define MIN_COLS 80
#define MIN_LINES 24

// ============ LOG BUFFER ============
#define MAX_LOGS 100
static char log_buffer[MAX_LOGS][256];
static int log_index = 0;
static char latest_log[256] = "[SYSTEM] Dashboard initialized";

// ============ MUTEX FOR LOG BUFFER ============
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============ UI INITIALIZATION ============
int ui_init(void) {
    // Initialize ncurses
    initscr();
    
    // Check minimum terminal size
    if (COLS < MIN_COLS || LINES < MIN_LINES) {
        endwin();
        fprintf(stderr, "[UI] Terminal too small: %dx%d (min: %dx%d)\n", 
                COLS, LINES, MIN_COLS, MIN_LINES);
        return -1;
    }
    
    cbreak();              // Disable line buffering
    noecho();              // Don't echo keystrokes
    curs_set(0);           // Hide cursor
    nodelay(stdscr, TRUE); // Non-blocking input
    keypad(stdscr, TRUE);  // Enable special keys
    
    // Start colors
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN, -1);    // OK status
        init_pair(2, COLOR_YELLOW, -1);   // Warning
        init_pair(3, COLOR_RED, -1);      // Error/Critical
        init_pair(4, COLOR_CYAN, -1);     // Info
        init_pair(5, COLOR_WHITE, COLOR_BLUE); // Header
    }
    
    // Create main window
    main_win = newwin(LINES, COLS, 0, 0);
    if (!main_win) {
        endwin();
        return -1;
    }
    
    // Calculate dimensions
    int workers_height = LINES - HEADER_HEIGHT - LOGS_HEIGHT - 2;
    if (workers_height < 5) workers_height = 5;
    
    // Create header window (top)
    header_win = newwin(HEADER_HEIGHT, COLS - 2, 1, 1);
    if (header_win) box(header_win, 0, 0);
    
    // Create workers table window (middle)
    workers_win = newwin(workers_height, COLS - 2, HEADER_HEIGHT + 1, 1);
    if (workers_win) box(workers_win, 0, 0);
    
    // Create logs window (bottom)
    logs_win = newwin(LOGS_HEIGHT, COLS - 2, LINES - LOGS_HEIGHT - 1, 1);
    if (logs_win) box(logs_win, 0, 0);
    
    // Create title
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

// ============ UI CLEANUP ============
void ui_cleanup(void) {
    // Restore terminal FIRST (critical!)
    endwin();
    
    // Clear all windows safely
    if (main_win) { delwin(main_win); main_win = NULL; }
    if (header_win) { delwin(header_win); header_win = NULL; }
    if (workers_win) { delwin(workers_win); workers_win = NULL; }
    if (logs_win) { delwin(logs_win); logs_win = NULL; }
    
    // Reset ncurses modes
    curs_set(1);   // Show cursor again
    nocbreak();    // Re-enable line buffering
    echo();        // Re-enable echo
    nodelay(stdscr, FALSE); // Blocking input
    
    // Clear screen
    clear();
    refresh();
}

// ============ LOG FUNCTIONS ============
const char* ui_get_latest_log(void) {
    return latest_log;
}

void ui_add_log(const char *format, ...) {
    char buffer[256];
    
    // Format the message
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = '\0';
    
    // Thread-safe update of log buffer
    pthread_mutex_lock(&log_mutex);
    
    strncpy(log_buffer[log_index], buffer, 255);
    log_buffer[log_index][255] = '\0';
    log_index = (log_index + 1) % MAX_LOGS;
    strncpy(latest_log, buffer, 255);
    latest_log[255] = '\0';
    
    pthread_mutex_unlock(&log_mutex);
}

// ============ SYSTEM INFO DRAWING ============
void ui_draw_system_info(void) {
    if (!header_win) return;
    
    // CLEAR window first
    werase(header_win);
    
    // Get statistics atomically
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
    
    // Draw with bounds checking
    box(header_win, 0, 0);
    
    int max_col = COLS - 2;
    if (max_col < 10) max_col = 10;
    
    wattron(header_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(header_win, 1, 2, "SYSTEM STATUS");
    wattroff(header_win, A_BOLD | COLOR_PAIR(4));
    
    // Time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    if (max_col > 20) {
        mvwprintw(header_win, 1, max_col - 15, "%s", time_str);
    }
    
    // Stats line
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
    
    // Refresh this window only
    wnoutrefresh(header_win);
}
// ============ WORKERS TABLE DRAWING ============
void ui_draw_workers_table(void) {
    if (!workers_win || !shared_stats || semid == -1) return;
    
    // CLEAR completely
    werase(workers_win);
    box(workers_win, 0, 0);
    
    // Create atomic snapshot
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
    
    // Header
    wattron(workers_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(workers_win, 1, 1, "%-3s %-6s %-8s %-6s %-8s", 
              "ID", "PID", "Health", "Tasks", "Status");
    wattroff(workers_win, A_BOLD | COLOR_PAIR(4));
    
    // Separator line
    int width = COLS - 4;
    if (width > 2 && width < 200) {
        for (int i = 1; i < width; i++) {
            mvwaddch(workers_win, 2, i, ACS_HLINE);
        }
    }
    
    // Draw workers
    int max_rows = LINES - HEADER_HEIGHT - LOGS_HEIGHT - 6;
    if (max_rows < 1) max_rows = 1;
    
    for (int i = 0; i < MAX_WORKERS && i < max_rows; i++) {
        int row = i + 3;
        
        // Format status
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
        
        // Draw with color
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

// ============ LOGS CONSOLE DRAWING ============
void ui_draw_logs_console(void) {
    if (!logs_win) return;
    
    // CLEAR completely
    werase(logs_win);
    box(logs_win, 0, 0);
    
    // Title
    wattron(logs_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(logs_win, 0, 2, " LIVE LOGS ");
    wattroff(logs_win, A_BOLD | COLOR_PAIR(4));
    
    // Thread-safe read
    pthread_mutex_lock(&log_mutex);
    
    int visible_lines = LOGS_HEIGHT - 2;
    int start = (log_index >= visible_lines) ? (log_index - visible_lines) : 0;
    
    for (int i = start, line = 1; i < log_index && line < LOGS_HEIGHT - 1; i++, line++) {
        // Truncate to prevent wrap
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
        mvwprintw(logs_win, line, 2, "%-58s", msg);  // Fixed width
        wattroff(logs_win, COLOR_PAIR(4));
    }
    
    pthread_mutex_unlock(&log_mutex);
    
    wnoutrefresh(logs_win);
}

// ============ DASHBOARD DRAWING ============
void ui_draw_dashboard(void) {
    // Draw all sections
    ui_draw_system_info();
    ui_draw_workers_table();
    ui_draw_logs_console();
    
    // Single atomic screen update
    doupdate();
}

void ui_refresh(void) {
    // Clear stdscr to prevent ghosting
    clear();
    refresh();
    
    // Redraw everything
    ui_draw_dashboard();
    
    // Small delay
    usleep(50000);
}

// ============ TERMINAL RESIZE HANDLER ============
void ui_handle_resize(void) {
    // Delete old windows
    if (header_win) delwin(header_win);
    if (workers_win) delwin(workers_win);
    if (logs_win) delwin(logs_win);
    
    // Recreate
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
    
    // Force full redraw
    touchwin(header_win);
    touchwin(workers_win);
    touchwin(logs_win);
    clearok(stdscr, TRUE);
}