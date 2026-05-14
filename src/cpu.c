#include "cpu.h"
#include <stdio.h>
#include <string.h>

/* Raw tick counters read from /proc/stat */
typedef struct {
    long long user, nice, system, idle, iowait, irq, softirq, steal;
} CpuTicks;

static CpuTicks prev[MAX_CORES + 1];  /* index 0 = aggregate "cpu" line */
static int      num_cores = 0;

static long long total(const CpuTicks *t) {
    return t->user + t->nice + t->system + t->idle +
           t->iowait + t->irq + t->softirq + t->steal;
}

static long long idle_ticks(const CpuTicks *t) {
    return t->idle + t->iowait;
}

/* Read current tick values from /proc/stat into `cur`. */
static int read_stat(CpuTicks cur[], int *cores) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    char line[256];
    int  idx = 0;   /* 0 = aggregate, 1..N = per-core */

    while (fgets(line, sizeof(line), f) && idx <= MAX_CORES) {
        if (strncmp(line, "cpu", 3) != 0) break;

        CpuTicks t = {0};
        int parsed = sscanf(line, "%*s %lld %lld %lld %lld %lld %lld %lld %lld",
                            &t.user, &t.nice, &t.system, &t.idle,
                            &t.iowait, &t.irq, &t.softirq, &t.steal);
        if (parsed >= 4)
            cur[idx++] = t;
    }
    fclose(f);

    /* idx=0 is aggregate; idx-1 is count of per-core entries */
    *cores = idx - 1;
    return 0;
}

void cpu_sample(void) {
    read_stat(prev, &num_cores);
}

void cpu_get_info(CpuInfo *info) {
    CpuTicks cur[MAX_CORES + 1] = {0};
    int cores = 0;

    if (read_stat(cur, &cores) < 0) return;

    info->num_cores = cores;

    /* Aggregate usage (index 0) */
    long long dtotal = total(&cur[0]) - total(&prev[0]);
    long long didle  = idle_ticks(&cur[0]) - idle_ticks(&prev[0]);
    info->total_usage = dtotal > 0 ? (1.0 - (double)didle / dtotal) * 100.0 : 0.0;

    /* Per-core usage (indices 1..N) */
    for (int i = 0; i < cores && i < MAX_CORES; i++) {
        dtotal = total(&cur[i + 1]) - total(&prev[i + 1]);
        didle  = idle_ticks(&cur[i + 1]) - idle_ticks(&prev[i + 1]);
        info->usage[i] = dtotal > 0 ? (1.0 - (double)didle / dtotal) * 100.0 : 0.0;
    }

    /* Save current as previous for next call */
    memcpy(prev, cur, sizeof(prev));
    num_cores = cores;
}
