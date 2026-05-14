#include "display.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>

#define BAR_WIDTH 30

static void draw_bar(int y, int x, double pct, int color_pair) {
    int filled = (int)(pct / 100.0 * BAR_WIDTH);
    if (filled > BAR_WIDTH) filled = BAR_WIDTH;

    mvprintw(y, x, "[");
    attron(COLOR_PAIR(color_pair));
    for (int i = 0; i < BAR_WIDTH; i++)
        addch(i < filled ? '|' : ' ');
    attroff(COLOR_PAIR(color_pair));
    printw("] %5.1f%%", pct);
}

static int bar_color(double pct) {
    if (pct >= 80.0) return 3;   /* red */
    if (pct >= 50.0) return 2;   /* yellow */
    return 1;                    /* green */
}

void display_init(void) {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_GREEN,  COLOR_BLACK);
        init_pair(2, COLOR_YELLOW, COLOR_BLACK);
        init_pair(3, COLOR_RED,    COLOR_BLACK);
        init_pair(4, COLOR_CYAN,   COLOR_BLACK);
        init_pair(5, COLOR_WHITE,  COLOR_BLACK);
    }
}

void display_cleanup(void) {
    endwin();
}

void display_update(const CpuInfo *cpu, const MemInfo *mem, const ProcList *procs) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    clear();

    int row = 0;

    /* ── Header ── */
    attron(A_BOLD | COLOR_PAIR(4));
    mvprintw(row++, 0, " System Monitor  (press 'q' to quit)");
    attroff(A_BOLD | COLOR_PAIR(4));
    mvhline(row++, 0, ACS_HLINE, max_x);

    /* ── CPU Section ── */
    attron(A_BOLD);
    mvprintw(row++, 0, " CPU  (%d cores)", cpu->num_cores);
    attroff(A_BOLD);

    /* Overall */
    mvprintw(row, 2, "Total ");
    draw_bar(row++, 8, cpu->total_usage, bar_color(cpu->total_usage));

    /* Per-core (two columns if space allows) */
    int cols = (max_x >= 80) ? 2 : 1;
    for (int i = 0; i < cpu->num_cores && row < max_y - 10; i++) {
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

    double mem_pct = mem->used_percent;
    mvprintw(row, 2, "RAM   ");
    draw_bar(row++, 8, mem_pct, bar_color(mem_pct));
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

    /* ── Process Section ── */
    attron(A_BOLD);
    mvprintw(row++, 0, " Processes (%d)", procs->count);
    mvprintw(row++, 0, " %-7s %-20s %5s  %8s  %c",
             "PID", "Name", "CPU%", "Mem(MB)", 'S');
    attroff(A_BOLD);

    int shown = 0;
    for (int i = 0; i < procs->count && row < max_y - 1; i++) {
        const ProcInfo *p = &procs->procs[i];
        /* Skip idle/zombie processes with 0 cpu and 0 mem (kernel threads) */
        if (p->cpu_percent < 0.01 && p->mem_kb == 0) continue;

        mvprintw(row++, 0, " %-7d %-20.20s %5.1f  %8.2f  %c",
                 p->pid, p->name,
                 p->cpu_percent,
                 (double)p->mem_kb / 1024.0,
                 p->state);
        shown++;
        if (shown >= 20) break;
    }

    mvhline(max_y - 1, 0, ACS_HLINE, max_x);
    refresh();
}
