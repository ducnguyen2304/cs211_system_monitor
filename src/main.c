#include "cpu.h"
#include "memory.h"
#include "process.h"
#include "display.h"
#include <ncurses.h>
#include <unistd.h>

#define REFRESH_MS 1000   /* update every 1 second */

int main(void) {
    /* Take first sample (baseline for delta calculations). */
    cpu_sample();
    proc_sample();

    display_init();

    while (1) {
        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;

        usleep(REFRESH_MS * 1000);

        CpuInfo  cpu   = {0};
        MemInfo  mem   = {0};
        ProcList procs = {0};

        cpu_get_info(&cpu);
        mem_get_info(&mem);
        proc_get_list(&procs);

        /* Update baseline for next tick */
        cpu_sample();
        proc_sample();

        display_update(&cpu, &mem, &procs);
    }

    display_cleanup();
    return 0;
}
