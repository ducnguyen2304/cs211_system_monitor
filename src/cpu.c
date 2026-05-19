/**
 * @file cpu.c
 * @brief CPU utilisation sampling via /proc/stat.
 *
 * The Linux kernel exposes cumulative CPU tick counters in /proc/stat.
 * This module snapshots those counters twice and computes the fraction of
 * non-idle ticks in the interval, giving a usage percentage per core and
 * in aggregate.
 *
 * Formula: CPU% = (1 − Δidle / Δtotal) × 100
 */

#include "cpu.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Raw tick counters for a single CPU line in /proc/stat.
 *
 * Each field counts jiffies (kernel time units) spent in the
 * corresponding mode since boot.
 *
 * Fields (in /proc/stat column order):
 *   user    – normal user-space time
 *   nice    – low-priority user-space time
 *   system  – kernel time
 *   idle    – truly idle time
 *   iowait  – waiting on I/O (CPU is idle but stalled)
 *   irq     – servicing hardware interrupts
 *   softirq – servicing software interrupts
 *   steal   – time stolen by a hypervisor (meaningful on VMs)
 */
typedef struct {
    long long user, nice, system, idle, iowait, irq, softirq, steal;
} CpuTicks;

/* index 0 = aggregate "cpu" line; 1..MAX_CORES = per-core "cpuN" lines */
static CpuTicks prev[MAX_CORES + 1];
static int      num_cores = 0;

/** Sum all tick fields to obtain the total elapsed ticks for a snapshot. */
static long long total(const CpuTicks *t) {
    return t->user + t->nice + t->system + t->idle +
           t->iowait + t->irq + t->softirq + t->steal;
}

/**
 * @brief Return the number of idle ticks in a snapshot.
 *
 * iowait is counted as idle because the CPU is not executing instructions
 * while waiting for I/O to complete.
 */
static long long idle_ticks(const CpuTicks *t) {
    return t->idle + t->iowait;
}

/**
 * @brief Read all CPU tick lines from /proc/stat into @p cur.
 *
 * Reads the aggregate "cpu" line (index 0) followed by up to MAX_CORES
 * per-core "cpuN" lines.  Stops at the first non-"cpu" line.
 *
 * @param cur    Output array; must have capacity MAX_CORES + 1.
 * @param cores  Set to the number of per-core lines parsed (not counting index 0).
 * @return       0 on success, -1 if /proc/stat cannot be opened.
 */
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

    /* idx includes the aggregate row, so per-core count is idx − 1 */
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

    /* Aggregate usage: index 0 holds the "cpu" (all-core sum) line */
    long long dtotal = total(&cur[0]) - total(&prev[0]);
    long long didle  = idle_ticks(&cur[0]) - idle_ticks(&prev[0]);
    info->total_usage = dtotal > 0 ? (1.0 - (double)didle / dtotal) * 100.0 : 0.0;

    /* Per-core usage: indices 1..N correspond to "cpu0", "cpu1", … */
    for (int i = 0; i < cores && i < MAX_CORES; i++) {
        dtotal = total(&cur[i + 1]) - total(&prev[i + 1]);
        didle  = idle_ticks(&cur[i + 1]) - idle_ticks(&prev[i + 1]);
        info->usage[i] = dtotal > 0 ? (1.0 - (double)didle / dtotal) * 100.0 : 0.0;
    }

    /* Advance baseline so the next call computes the next interval's delta */
    memcpy(prev, cur, sizeof(prev));
    num_cores = cores;
}
