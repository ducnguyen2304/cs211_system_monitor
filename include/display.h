#ifndef DISPLAY_H
#define DISPLAY_H

#include "cpu.h"
#include "memory.h"
#include "process.h"

void display_init(void);
void display_cleanup(void);

/*
 * Render one frame.
 * selected     – index into the visible process list (rows that pass the filter)
 * show_threads – if non-zero, expand thread rows under the selected process
 * selected_pid – set to the PID of the selected visible row (-1 if none)
 */
void display_update(const CpuInfo *cpu, const MemInfo *mem, const ProcList *procs,
                    int selected, int show_threads, int *selected_pid);

/* Prompt helpers (use ncurses; call only from the main/UI thread). */
int display_prompt_confirm(const char *msg);           /* returns 1 if confirmed */
int display_prompt_int(const char *msg, int *out);     /* returns 0 on success   */

#endif
