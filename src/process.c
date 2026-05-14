#include "process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

#define PROC_FIELDS 52   /* number of fields in /proc/[pid]/stat */

typedef struct {
    int       pid;
    long long utime;   /* user-mode ticks */
    long long stime;   /* kernel-mode ticks */
} ProcTicks;

static ProcTicks prev_ticks[MAX_PROCS];
static int       prev_count = 0;
static long long prev_cpu_total = 0;

/* Read aggregate CPU ticks from /proc/stat (sum of all fields on "cpu" line). */
static long long read_cpu_total(void) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    long long vals[10] = {0};
    fscanf(f, "cpu %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
           &vals[0], &vals[1], &vals[2], &vals[3], &vals[4],
           &vals[5], &vals[6], &vals[7], &vals[8], &vals[9]);
    fclose(f);
    long long sum = 0;
    for (int i = 0; i < 10; i++) sum += vals[i];
    return sum;
}

static int is_pid_dir(const char *name) {
    for (int i = 0; name[i]; i++)
        if (!isdigit((unsigned char)name[i])) return 0;
    return name[0] != '\0';
}

/* Read /proc/[pid]/stat and fill ProcInfo fields (no cpu%). */
static int read_proc_stat(int pid, ProcInfo *p, ProcTicks *t) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char comm[MAX_NAME];
    char state;
    long long utime, stime;
    /* fields we skip: ppid, pgrp, session, tty_nr, tpgid, flags,
       minflt, cminflt, majflt, cmajflt = 10 fields after state */
    long skip;
    long long lskip;

    int r = fscanf(f, "%d (%63[^)]) %c %ld %ld %ld %ld %ld %lu "
                      "%lu %lu %lu %lu %lld %lld",
                   &pid, comm, &state,
                   &skip, &skip, &skip, &skip, &skip,   /* ppid..tpgid */
                   (unsigned long *)&skip,               /* flags */
                   (unsigned long *)&skip, (unsigned long *)&skip,
                   (unsigned long *)&skip, (unsigned long *)&skip, /* faults */
                   &utime, &stime);
    fclose(f);

    if (r < 15) return -1;

    p->pid   = pid;
    p->state = state;
    strncpy(p->name, comm, MAX_NAME - 1);
    p->name[MAX_NAME - 1] = '\0';

    t->pid   = pid;
    t->utime = utime;
    t->stime = stime;
    return 0;
}

/* Read /proc/[pid]/status for VmRSS (resident memory). */
static long read_proc_mem(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char key[64];
    long value = 0;
    while (fscanf(f, "%63s %ld kB\n", key, &value) >= 1) {
        if (strcmp(key, "VmRSS:") == 0) break;
        value = 0;
    }
    fclose(f);
    return value;
}

void proc_sample(void) {
    DIR *dir = opendir("/proc");
    if (!dir) return;

    prev_count     = 0;
    prev_cpu_total = read_cpu_total();

    struct dirent *entry;
    while ((entry = readdir(dir)) && prev_count < MAX_PROCS) {
        if (!is_pid_dir(entry->d_name)) continue;
        int pid = atoi(entry->d_name);
        ProcInfo dummy;
        read_proc_stat(pid, &dummy, &prev_ticks[prev_count++]);
    }
    closedir(dir);
}

static int cmp_cpu(const void *a, const void *b) {
    const ProcInfo *pa = (const ProcInfo *)a;
    const ProcInfo *pb = (const ProcInfo *)b;
    if (pb->cpu_percent > pa->cpu_percent) return  1;
    if (pb->cpu_percent < pa->cpu_percent) return -1;
    return 0;
}

void proc_get_list(ProcList *list) {
    DIR *dir = opendir("/proc");
    if (!dir) return;

    long long cpu_total = read_cpu_total();
    long long delta_cpu = cpu_total - prev_cpu_total;

    list->count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) && list->count < MAX_PROCS) {
        if (!is_pid_dir(entry->d_name)) continue;
        int pid = atoi(entry->d_name);

        ProcInfo  p   = {0};
        ProcTicks cur = {0};
        if (read_proc_stat(pid, &p, &cur) < 0) continue;

        p.mem_kb = read_proc_mem(pid);

        /* Match against previous sample to compute cpu% */
        p.cpu_percent = 0.0;
        for (int i = 0; i < prev_count; i++) {
            if (prev_ticks[i].pid == pid) {
                long long delta_proc = (cur.utime + cur.stime) -
                                       (prev_ticks[i].utime + prev_ticks[i].stime);
                if (delta_cpu > 0)
                    p.cpu_percent = (double)delta_proc / delta_cpu * 100.0;
                break;
            }
        }

        list->procs[list->count++] = p;
    }
    closedir(dir);

    /* Sort by CPU usage descending */
    qsort(list->procs, list->count, sizeof(ProcInfo), cmp_cpu);
}
