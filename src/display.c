/**
 * @file display.c
 * @brief ncurses-based terminal UI rendering.
 *
 * Renders a full-screen system monitor UI with three sections:
 *   1. CPU  – total utilisation bar and one bar per logical core.
 *   2. Memory – RAM and swap utilisation bars with raw MB figures.
 *   3. Processes – scrollable table sorted by CPU%, with optional
 *      per-thread expansion for the selected row.
 *
 * All public functions must be called from the UI (main) thread only.
 * ncurses is not thread-safe and shares global display state.
 */

#include "display.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>

/* SCHED_BATCH and SCHED_IDLE are Linux-specific; guard for portability. */
#ifndef SCHED_BATCH
#define SCHED_BATCH 3
#endif
#ifndef SCHED_IDLE
#define SCHED_IDLE 5
#endif

/** Width in characters of each filled/unfilled progress bar. */
#define BAR_WIDTH 28

/* Ring buffer tracking the 5 most recently active processes. */
#define RECENT_MAX 5
typedef struct {
    int    pid;
    char   name[64];
    double cpu_percent;
} RecentEntry;
static RecentEntry recent_active[RECENT_MAX];
static int         recent_count = 0;

static void update_recent(int pid, const char *name, double cpu_pct) {
    /* Remove existing entry for this pid, if any. */
    for (int i = 0; i < recent_count; i++) {
        if (recent_active[i].pid == pid) {
            memmove(&recent_active[i], &recent_active[i + 1],
                    (recent_count - i - 1) * sizeof(RecentEntry));
            recent_count--;
            break;
        }
    }
    /* Shift down to make room at index 0 (most recent). */
    if (recent_count == RECENT_MAX)
        memmove(&recent_active[1], &recent_active[0],
                (RECENT_MAX - 1) * sizeof(RecentEntry));
    else
        memmove(&recent_active[1], &recent_active[0],
                recent_count * sizeof(RecentEntry));
    recent_active[0].pid = pid;
    strncpy(recent_active[0].name, name, sizeof(recent_active[0].name) - 1);
    recent_active[0].name[sizeof(recent_active[0].name) - 1] = '\0';
    recent_active[0].cpu_percent = cpu_pct;
    if (recent_count < RECENT_MAX) recent_count++;
}

/* ncurses colour pair identifiers. */
#define CP_GREEN  1
#define CP_YELLOW 2
#define CP_RED    3
#define CP_CYAN   4
#define CP_WHITE  5
#define CP_BLUE   6

/**
 * @brief Draw a horizontal progress bar at the given screen position.
 *
 * Renders: "[||||||||||||              ]  42.3%"
 *
 * @param y   Screen row.
 * @param x   Screen column where the leading '[' is placed.
 * @param pct Fill percentage in [0.0, 100.0]; clamped at BAR_WIDTH.
 * @param cp  ncurses colour pair to apply to the filled segment.
 */
static void draw_bar(int y, int x, double pct, int cp) {
    int filled = (int)(pct / 100.0 * BAR_WIDTH);
    if (filled > BAR_WIDTH) filled = BAR_WIDTH;
    mvprintw(y, x, "[");
    attron(COLOR_PAIR(cp));
    for (int i = 0; i < BAR_WIDTH; i++)
        addch(i < filled ? '|' : ' ');
    attroff(COLOR_PAIR(cp));
    printw("] %5.1f%%", pct);
}

/**
 * @brief Choose a bar colour based on utilisation level.
 *
 * Returns CP_RED for ≥ 80%, CP_YELLOW for ≥ 50%, CP_GREEN otherwise.
 *
 * @param pct  Utilisation percentage.
 * @return     ncurses colour pair constant.
 */
static int bar_color(double pct) {
    if (pct >= 80.0) return CP_RED;
    if (pct >= 50.0) return CP_YELLOW;
    return CP_GREEN;
}

/**
 * @brief Convert a Linux scheduling policy constant to a short display label.
 *
 * @param policy  SCHED_* constant as returned by sched_getscheduler(2).
 * @return        A 3-character string; "?  " for unknown policies.
 */
static const char *policy_str(int policy) {
    switch (policy) {
        case SCHED_OTHER:    return "NRM";
        case SCHED_FIFO:     return "FIF";
        case SCHED_RR:       return "RR ";
        case SCHED_BATCH:    return "BAT";
        case SCHED_IDLE:     return "IDL";
#ifdef SCHED_DEADLINE
        case SCHED_DEADLINE: return "DL ";
#endif
        default:             return "?  ";
    }
}

void display_init(void) {
    initscr();
    /*
     * halfdelay(2): getch() blocks for up to 200 ms before returning ERR.
     * This keeps the UI responsive to key events without busy-waiting,
     * while still allowing the main loop to poll for new sampler data.
     */
    halfdelay(2);
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);

    if (has_colors()) {
        start_color();
        init_pair(CP_GREEN,  COLOR_GREEN,  COLOR_BLACK);
        init_pair(CP_YELLOW, COLOR_YELLOW, COLOR_BLACK);
        init_pair(CP_RED,    COLOR_RED,    COLOR_BLACK);
        init_pair(CP_CYAN,   COLOR_CYAN,   COLOR_BLACK);
        init_pair(CP_WHITE,  COLOR_WHITE,  COLOR_BLACK);
        init_pair(CP_BLUE,   COLOR_BLUE,   COLOR_BLACK);
    }
}

void display_cleanup(void) {
    endwin();
}

/* ── Prompt helpers ────────────────────────────────────────────────────── */

int display_prompt_confirm(const char *msg) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_x;

    /*
     * Switch to blocking + echo mode for the duration of the prompt so the
     * user's keypress is captured immediately.  Restore halfdelay + noecho
     * afterwards to resume normal UI behaviour.
     */
    cbreak();
    echo();
    curs_set(1);
    nodelay(stdscr, FALSE);

    move(max_y - 1, 0);
    clrtoeol();
    attron(A_BOLD | COLOR_PAIR(CP_RED));
    mvprintw(max_y - 1, 0, " %s [y/N] ", msg);
    attroff(A_BOLD | COLOR_PAIR(CP_RED));
    refresh();

    int ch = getch();

    noecho();
    curs_set(0);
    halfdelay(2);

    return (ch == 'y' || ch == 'Y');
}

int display_prompt_int(const char *msg, int *out) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_x;

    cbreak();
    echo();
    curs_set(1);
    nodelay(stdscr, FALSE);

    move(max_y - 1, 0);
    clrtoeol();
    attron(A_BOLD | COLOR_PAIR(CP_CYAN));
    mvprintw(max_y - 1, 0, " %s: ", msg);
    attroff(A_BOLD | COLOR_PAIR(CP_CYAN));
    refresh();

    char buf[32] = {0};
    wgetnstr(stdscr, buf, (int)sizeof(buf) - 1);

    noecho();
    curs_set(0);
    halfdelay(2);

    char *end;
    long val = strtol(buf, &end, 10);
    if (end == buf) return -1;   /* user entered no digits */
    *out = (int)val;
    return 0;
}

/* ── Main display ──────────────────────────────────────────────────────── */

void display_update(const CpuInfo *cpu, const MemInfo *mem, const ProcList *procs,
                    int selected, int show_threads, int *selected_pid) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    erase();

    /*
     * Build the visible process list by filtering out entries with
     * effectively zero CPU and zero memory — these are typically kernel
     * threads or zombie processes that add noise without useful data.
     */
    int vis[MAX_PROCS];
    int vis_count = 0;
    for (int i = 0; i < procs->count; i++) {
        const ProcInfo *p = &procs->procs[i];
        if (p->cpu_percent < 0.01 && p->mem_kb == 0) continue;
        vis[vis_count++] = i;
    }

    /* Clamp the cursor to the visible range so it never points off-screen. */
    if (vis_count == 0) selected = 0;
    else if (selected >= vis_count) selected = vis_count - 1;
    else if (selected < 0)          selected = 0;

    if (selected_pid)
        *selected_pid = (vis_count > 0) ? procs->procs[vis[selected]].pid : -1;

    int row = 0;

    /* ── Header ── */
    attron(A_BOLD | COLOR_PAIR(CP_CYAN));
    mvprintw(row++, 0, " System Monitor  (q=quit  ↑↓=select  t=threads  k=kill  r=renice  s=signal)");
    attroff(A_BOLD | COLOR_PAIR(CP_CYAN));
    mvhline(row++, 0, ACS_HLINE, max_x);

    /* ── CPU Section ── */
    attron(A_BOLD);
    mvprintw(row++, 0, " CPU  (%d cores)", cpu->num_cores);
    attroff(A_BOLD);

    mvprintw(row, 2, "Total ");
    draw_bar(row++, 8, cpu->total_usage, bar_color(cpu->total_usage));

    /*
     * Lay out per-core bars in two columns on wide terminals (≥ 80 chars)
     * to make better use of horizontal space.
     */
    int cols = (max_x >= 80) ? 2 : 1;
    for (int i = 0; i < cpu->num_cores && row < max_y - 12; i++) {
        int col = i % cols;
        int cx  = col * (max_x / cols);
        if (col == 0 && i != 0) row++;
        mvprintw(row, cx + 2, "Core%-2d", i);
        draw_bar(row, cx + 9, cpu->usage[i], bar_color(cpu->usage[i]));
        if (col == cols - 1 || i == cpu->num_cores - 1) row++;
    }
    mvhline(row++, 0, ACS_HLINE, max_x);

    /* ── Memory Section ── */
    attron(A_BOLD);
    mvprintw(row++, 0, " Memory");
    attroff(A_BOLD);

    mvprintw(row, 2, "RAM   ");
    draw_bar(row++, 8, mem->used_percent, bar_color(mem->used_percent));
    mvprintw(row++, 2, "  %ld MB used / %ld MB total  (avail: %ld MB)",
             mem->used_kb / 1024, mem->total_kb / 1024, mem->available_kb / 1024);

    if (mem->swap_total_kb > 0) {
        double swap_pct = (double)mem->swap_used_kb / mem->swap_total_kb * 100.0;
        mvprintw(row, 2, "Swap  ");
        draw_bar(row++, 8, swap_pct, bar_color(swap_pct));
        mvprintw(row++, 2, "  %ld MB used / %ld MB total",
                 mem->swap_used_kb / 1024, mem->swap_total_kb / 1024);
    }
    mvhline(row++, 0, ACS_HLINE, max_x);

    /* ── Top 5 Recent Processes ── */
    {
        /* Update ring buffer with every process that has non-zero CPU. */
        for (int vi = vis_count - 1; vi >= 0; vi--) {
            const ProcInfo *p = &procs->procs[vis[vi]];
            if (p->cpu_percent >= 0.01)
                update_recent(p->pid, p->name, p->cpu_percent);
        }

        attron(A_BOLD | COLOR_PAIR(CP_CYAN));
        mvprintw(row, 0, " Top 5 Recent:");
        attroff(A_BOLD | COLOR_PAIR(CP_CYAN));

        int chip_w = (max_x - 2) / 5;
        if (chip_w < 14) chip_w = 14;
        int cx = 16;
        for (int i = 0; i < recent_count; i++) {
            int cp = bar_color(recent_active[i].cpu_percent);
            attron(COLOR_PAIR(cp) | A_BOLD);
            mvprintw(row, cx, "[%-.*s %.1f%%]",
                     chip_w - 9, recent_active[i].name,
                     recent_active[i].cpu_percent);
            attroff(COLOR_PAIR(cp) | A_BOLD);
            cx += chip_w;
        }
        row++;
    }
    mvhline(row++, 0, ACS_HLINE, max_x);

    /* ── Process Table ── */
    attron(A_BOLD);
    mvprintw(row++, 0, " Processes (%d)", procs->count);
    mvprintw(row++, 0, " %-7s %-18s %5s  %8s  %c  %3s %3s  %-4s %3s",
             "PID", "Name", "CPU%", "Mem(MB)", 'S', "NI", "PRI", "POL", "THR");
    attroff(A_BOLD);

    int proc_start_row = row;

    for (int vi = 0; vi < vis_count && row < max_y - 2; vi++) {
        const ProcInfo *p = &procs->procs[vis[vi]];
        int is_selected   = (vi == selected);

        if (is_selected) attron(A_REVERSE);

        mvprintw(row++, 0, " %-7d %-18.18s %5.1f  %8.2f  %c  %3d %3d  %-4s %3d",
                 p->pid, p->name,
                 p->cpu_percent,
                 (double)p->mem_kb / 1024.0,
                 p->state,
                 p->nice, p->priority,
                 policy_str(p->sched_policy),
                 p->num_threads);

        if (is_selected) attroff(A_REVERSE);

        /* Expand thread rows beneath the selected process when requested. */
        if (is_selected && show_threads && p->num_threads > 0) {
            attron(COLOR_PAIR(CP_BLUE));
            for (int ti = 0; ti < p->num_threads && row < max_y - 2; ti++) {
                const ThreadInfo *th = &p->threads[ti];
                mvprintw(row++, 4, "  TID:%-7d  state:%c  cpu:%5.1f%%",
                         th->tid, th->state, th->cpu_percent);
            }
            attroff(COLOR_PAIR(CP_BLUE));
        }
    }
    (void)proc_start_row;

    /* ── Status bar ── */
    mvhline(max_y - 2, 0, ACS_HLINE, max_x);
    attron(COLOR_PAIR(CP_CYAN));
    mvprintw(max_y - 1, 0,
             " q:quit  arrows:select  t:threads  k:kill  r:renice  s:signal(%d sel)",
             selected_pid ? *selected_pid : -1);
    attroff(COLOR_PAIR(CP_CYAN));

    refresh();
}
