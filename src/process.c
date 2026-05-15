#include "process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <sched.h>
#include <sys/resource.h>

typedef struct {
    int       tid;
    long long utime;
    long long stime;
} ThreadTick;

typedef struct {
    int        pid;
    long long  utime;
    long long  stime;
    ThreadTick thread_ticks[MAX_THREADS];
    int        num_thread_ticks;
} ProcTicks;

static ProcTicks prev_ticks[MAX_PROCS];
static int       prev_count     = 0;
static long long prev_cpu_total = 0;

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

/*
 * Parse a /proc/[pid]/stat (or /proc/[pid]/task/[tid]/stat) file.
 * Reads through field 20 (num_threads). All out-pointers are optional.
 * Returns 0 on success, -1 on failure.
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

    /* fields 1-20 of /proc/pid/stat */
    int r = fscanf(f, "%d (%63[^)]) %c "
                      "%ld %ld %ld %ld %ld %lu "   /* ppid..flags */
                      "%lu %lu %lu %lu "            /* minflt..cmajflt */
                      "%lld %lld "                  /* utime stime */
                      "%lld %lld "                  /* cutime cstime */
                      "%ld %ld %ld",                /* priority nice num_threads */
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

/*
 * Scan /proc/[pid]/task/ for all threads.
 * Fills t->thread_ticks[] and (if p != NULL) p->threads[] in parallel.
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

        /* scheduling policy via syscall */
        int policy = sched_getscheduler(pid);
        p.sched_policy = (policy >= 0) ? policy : 0;
        if (policy == SCHED_FIFO || policy == SCHED_RR) {
            struct sched_param sp = {0};
            if (sched_getparam(pid, &sp) == 0)
                p.rt_priority = sp.sched_priority;
        }

        /* find previous sample for this pid */
        ProcTicks *prev = NULL;
        for (int i = 0; i < prev_count; i++) {
            if (prev_ticks[i].pid == pid) { prev = &prev_ticks[i]; break; }
        }

        /* process cpu% */
        if (prev && delta_cpu > 0) {
            long long dp = (cur_utime + cur_stime) - (prev->utime + prev->stime);
            p.cpu_percent = (double)dp / delta_cpu * 100.0;
        }

        /* collect thread ticks for this sample (parallel to p.threads[]) */
        ProcTicks cur_ticks = {0};
        cur_ticks.pid   = pid;
        cur_ticks.utime = cur_utime;
        cur_ticks.stime = cur_stime;
        read_proc_threads(pid, &cur_ticks, &p);

        /* per-thread cpu% using same delta_cpu denominator */
        if (prev && delta_cpu > 0) {
            for (int ti = 0; ti < cur_ticks.num_thread_ticks; ti++) {
                int tid = cur_ticks.thread_ticks[ti].tid;
                long long cur_tt = cur_ticks.thread_ticks[ti].utime
                                 + cur_ticks.thread_ticks[ti].stime;
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
