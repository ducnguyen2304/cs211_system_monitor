/**
 * @file display.h
 * @brief ncurses-based terminal UI interface.
 *
 * Renders CPU bars, memory bars, and a scrollable process table using
 * ncurses colour pairs.  All functions in this header must be called
 * from the main (UI) thread only — ncurses is not thread-safe.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "cpu.h"
#include "memory.h"
#include "process.h"

/**
 * @brief Initialise the ncurses display.
 *
 * Calls initscr(), enables colour, sets halfdelay(2) so getch() times
 * out after 200 ms, and hides the cursor.  Must be called once before
 * any other display function.
 */
void display_init(void);

/**
 * @brief Tear down the ncurses display and restore the terminal.
 *
 * Calls endwin().  Should be called before the process exits so the
 * terminal is left in a usable state.
 */
void display_cleanup(void);

/**
 * @brief Render one complete UI frame.
 *
 * Draws the CPU section (total + per-core bars), memory section
 * (RAM + swap bars), and the process table.  Optionally expands
 * per-thread rows beneath the selected process.
 *
 * @param cpu           CPU snapshot to display.
 * @param mem           Memory snapshot to display.
 * @param procs         Process list snapshot to display.
 * @param selected      Row index within the visible (filtered) process list.
 * @param show_threads  Non-zero to expand thread rows under the selected process.
 * @param selected_pid  Set to the PID of the selected row; set to -1 if none.
 */
void display_update(const CpuInfo *cpu, const MemInfo *mem, const ProcList *procs,
                    int selected, int show_threads, int *selected_pid);

/**
 * @brief Show a yes/no confirmation prompt on the status line.
 *
 * Temporarily switches ncurses to blocking, echo mode to capture one
 * keystroke, then restores the normal halfdelay + noecho settings.
 *
 * @param msg  Prompt text displayed to the user.
 * @return     1 if the user pressed 'y' or 'Y', 0 otherwise.
 */
int display_prompt_confirm(const char *msg);

/**
 * @brief Prompt the user for an integer value on the status line.
 *
 * Temporarily switches ncurses to blocking, echo mode to read a short
 * string, then restores normal halfdelay + noecho settings.
 *
 * @param msg  Prompt text displayed to the user.
 * @param out  Receives the parsed integer value on success.
 * @return     0 on success, -1 if no valid integer was entered.
 */
int display_prompt_int(const char *msg, int *out);

#endif /* DISPLAY_H */
