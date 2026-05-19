/**
 * @file process.c
 * @brief Process and thread enumeration via /proc.
 *
 * CPU usage is computed as a fraction of the total system CPU ticks consumed
 * in the interval between proc_sample() and proc_get_list().  The same
 * delta_cpu denominator is used for both process-level and thread-level
 * percentages so all values are on a consistent 0–100 scale regardless of
 * the number of cores.
 *
 * Data flow:
 *   proc_sample()    → reads /proc/[pid]/stat and /proc/[pid]/task/[tid]/stat
 *                      for every visible PID; stores tick counters in prev_ticks[].
 *   proc_get_list()  → reads the same files again, diffs against prev_ticks[],
 *                      and fills a ProcList sorted by descending CPU%.
 */

#include "process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <sched.h>
#include <sys/resource.h>

/** Tick counters for a single thread, used to compute per-thread CPU deltas. */
typedef struct {
    int       tid;
    long long utime;
    long long stime;
} ThreadTick;

/** Tick counters for a process and all of its threads. */
typedef struct {
    int        pid;
    long long  utime;
    long long  stime;
    ThreadTick thread_ticks[MAX_THREADS];
    int        num_thread_ticks;
} ProcTicks;

/* Baseline snapshots captured by the last proc_sample() call. */
static ProcTicks prev_ticks[MAX_PROCS];
static int       prev_count     = 0;
static long long prev_cpu_total = 0;

/**
 * @brief Read the total system CPU tick count from /proc/stat.
 *
 * Sums all ten tick fields of the aggregate "cpu" line.  Used as the
 * denominator when computing per-process CPU percentages so that the
 * result is naturally normalised across all cores.
 *
 * @return Total ticks since boot, or 0 if /proc/stat cannot be opened.
 */
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

/**
 * @brief Return non-zero if @p name is a non-empty decimal string.
 *
 * Used to identify PID/TID directories inside /proc (and /proc/[pid]/task)
 * while skipping ".", "..", and named entries like "self".
 */
static int is_pid_dir(const char *name) {
    for (int i = 0; name[i]; i++)
        if (!isdigit((unsigned char)name[i])) return 0;
    return name[0] != '\0';
}

/**
 * @brief Parse fields 1–20 from a /proc/[pid]/stat (or task/[tid]/stat) file.
 *
 * All output parameters are optional; pass NULL for any field not needed.
 * The function reads through field 20 (num_threads) even when only earlier
 * fields are requested so the format string always consumes the correct input.
 *
 * @param path            Path to the stat file.
 * @param pid_hint        Fallback PID/TID used if the file cannot be parsed.
 * @param name_out        Receives the command name (stripped of parentheses).
 * @param state_out       Receives the single-character process state.
 * @param utime_out       Receives user-mode ticks (field 14).
 * @param stime_out       Receives kernel-mode ticks (field 15).
 * @param priority_out    Receives the kernel priority (field 18).
 * @param nice_out        Receives the nice value (field 19).
 * @param num_threads_out Receives the thread count (field 20).
 * @return                0 on success, -1 if the file could not be read or
 *                        fewer than 15 fields were successfully parsed.
 */
static int read_stat_file(const char *path, int pid_hint,
                           char *name_out, char *state_out,
                           long long *utime_out, long long *stime_out,
                           int *priority_out, int *nice_out,
                           int *num_threads_out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char      comm[MAX_NAME] = {0};
    char      state          = '?';
    long long utime = 0, stime = 0, cutime_s = 0, cstime_s = 0;
    long      priority_v = 0, nice_v = 0, num_threads_v = 0;
    long      skip_l;
    unsigned long skip_ul;
    int       pid = pid_hint;

    /*
     * /proc/pid/stat format (selected fields):
     *   1  pid           2  comm (name in parens)  3  state
     *   4  ppid          5  pgrp                   6  session
     *   7  tty_nr        8  tpgid                  9  flags
     *  10  minflt       11  cminflt               12  majflt
     *  13  cmajflt      14  utime                 15  stime
     *  16  cutime       17  cstime                18  priority
     *  19  nice         20  num_threads
     */
    int r = fscanf(f, "%d (%63[^)]) %c "
                      "%ld %ld %ld %ld %ld %lu "
                      "%lu %lu %lu %lu "
                      "%lld %lld "
                      "%lld %lld "
                      "%ld %ld %ld",
                   &pid, comm, &state,
                   &skip_l, &skip_l, &skip_l, &skip_l, &skip_l, &skip_ul,
                   &skip_ul, &skip_ul, &skip_ul, &skip_ul,
                   &utime, &stime,
                   &cutime_s, &cstime_s,
                   &priority_v, &nice_v, &num_threads_v);
    fclose(f);

    if (r < 15) return -1;

    if (name_out)        { strncpy(name_out, comm, MAX_NAME - 1); name_out[MAX_NAME-1] = '\0'; }
    if (state_out)       *state_out       = state;
    if (utime_out)       *utime_out       = utime;
    if (stime_out)       *stime_out       = stime;
    if (priority_out)    *priority_out    = (r >= 18) ? (int)priority_v    : 0;
    if (nice_out)        *nice_out        = (r >= 19) ? (int)nice_v        : 0;
    if (num_threads_out) *num_threads_out = (r >= 20) ? (int)num_threads_v : 0;
    return 0;
}

/**
 * @brief Read the resident set size (VmRSS) for process @p pid.
 *
 * VmRSS is the portion of the process's address space that currently
 * resides in physical RAM, excluding swapped-out pages.
 *
 * @param pid  Target process ID.
 * @return     VmRSS in kibibytes, or 0 if the file cannot be read.
 */
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

/**
 * @brief Enumerate all threads of @p pid and record their tick counters.
 *
 * Scans /proc/[pid]/task/ and reads each thread's stat file.  Results are
 * stored in @p t (raw ticks) and optionally in @p p (display-ready ThreadInfo
 * structs with cpu_percent initialised to 0.0).
 *
 * @param pid  Process whose thread directory is scanned.
 * @param t    Output ProcTicks struct; thread_ticks[] is populated.
 * @param p    Optional output ProcInfo struct; threads[] is populated when non-NULL.
 */
static void read_proc_threads(int pid, ProcTicks *t, ProcInfo *p) {
    char dir_path[64];
    snprintf(dir_path, sizeof(dir_path), "/proc/%d/task", pid);
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    t->num_thread_ticks = 0;
    if (p) p->num_threads = 0;

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!is_pid_dir(entry->d_name)) continue;
        int tid = atoi(entry->d_name);

        char stat_path[128];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/task/%d/stat", pid, tid);

        char      state = '?';
        long long utime = 0, stime = 0;
        read_stat_file(stat_path, tid, NULL, &state, &utime, &stime,
                       NULL, NULL, NULL);

        if (t->num_thread_ticks < MAX_THREADS) {
            int idx = t->num_thread_ticks++;
            t->thread_ticks[idx].tid   = tid;
            t->thread_ticks[idx].utime = utime;
            t->thread_ticks[idx].stime = stime;

            if (p) {
                p->threads[idx].tid         = tid;
                p->threads[idx].state       = state;
                p->threads[idx].cpu_percent = 0.0;
                p->num_threads = t->num_thread_ticks;
            }
        }
    }
    closedir(dir);
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

        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);

        ProcTicks *t = &prev_ticks[prev_count];
        t->pid              = pid;
        t->num_thread_ticks = 0;

        long long ut = 0, st = 0;
        if (read_stat_file(path, pid, NULL, NULL, &ut, &st,
                           NULL, NULL, NULL) == 0) {
            t->utime = ut;
            t->stime = st;
            read_proc_threads(pid, t, NULL);
            prev_count++;
        }
    }
    closedir(dir);
}

/** Compare two ProcInfo entries by descending cpu_percent for qsort. */
static int cmp_cpu(const void *a, const void *b) {
    const ProcInfo *pa = a, *pb = b;
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

        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);

        ProcInfo  p         = {0};
        long long cur_utime = 0, cur_stime = 0;

        if (read_stat_file(path, pid, p.name, &p.state,
                           &cur_utime, &cur_stime,
                           &p.priority, &p.nice, NULL) < 0) continue;
        p.pid    = pid;
        p.mem_kb = read_proc_mem(pid);

        /* Retrieve the scheduling policy; fall back to SCHED_OTHER on error. */
        int policy = sched_getscheduler(pid);
        p.sched_policy = (policy >= 0) ? policy : 0;
        if (policy == SCHED_FIFO || policy == SCHED_RR) {
            struct sched_param sp = {0};
            if (sched_getparam(pid, &sp) == 0)
                p.rt_priority = sp.sched_priority;
        }

        /* Locate the baseline tick record for this PID (linear scan is fine
         * for MAX_PROCS ≤ 512 entries measured once per second). */
        ProcTicks *prev = NULL;
        for (int i = 0; i < prev_count; i++) {
            if (prev_ticks[i].pid == pid) { prev = &prev_ticks[i]; break; }
        }

        /* Process-level CPU%: Δ(utime+stime) / Δtotal_cpu × 100 */
        if (prev && delta_cpu > 0) {
            long long dp = (cur_utime + cur_stime) - (prev->utime + prev->stime);
            p.cpu_percent = (double)dp / delta_cpu * 100.0;
        }

        /* Collect current thread ticks and compute per-thread CPU% in parallel. */
        ProcTicks cur_ticks = {0};
        cur_ticks.pid   = pid;
        cur_ticks.utime = cur_utime;
        cur_ticks.stime = cur_stime;
        read_proc_threads(pid, &cur_ticks, &p);

        if (prev && delta_cpu > 0) {
            for (int ti = 0; ti < cur_ticks.num_thread_ticks; ti++) {
                int tid = cur_ticks.thread_ticks[ti].tid;
                long long cur_tt = cur_ticks.thread_ticks[ti].utime
                                 + cur_ticks.thread_ticks[ti].stime;
                /* Find the matching baseline thread entry. */
                long long prev_tt = 0;
                for (int pi = 0; pi < prev->num_thread_ticks; pi++) {
                    if (prev->thread_ticks[pi].tid == tid) {
                        prev_tt = prev->thread_ticks[pi].utime
                                + prev->thread_ticks[pi].stime;
                        break;
                    }
                }
                if (ti < MAX_THREADS)
                    p.threads[ti].cpu_percent = (double)(cur_tt - prev_tt) / delta_cpu * 100.0;
            }
        }

        list->procs[list->count++] = p;
    }
    closedir(dir);

    qsort(list->procs, list->count, sizeof(ProcInfo), cmp_cpu);
}
