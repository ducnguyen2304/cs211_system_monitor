/**
 * @file process.h
 * @brief Process and thread enumeration interface.
 *
 * Provides a two-call API (baseline + snapshot) for computing per-process
 * and per-thread CPU usage by diffing /proc tick counters over a measured
 * wall-clock interval.  Memory usage is read from /proc/[pid]/status.
 */

#ifndef PROCESS_H
#define PROCESS_H

/** Maximum number of processes tracked in a single snapshot. */
#define MAX_PROCS   512

/** Maximum number of threads tracked per process. */
#define MAX_THREADS  64

/** Maximum length of a process/command name, including NUL terminator. */
#define MAX_NAME     64

/**
 * @brief Per-thread CPU and state information.
 *
 * @var ThreadInfo::tid         Thread ID as reported by the kernel.
 * @var ThreadInfo::state       Single-character process state (R/S/D/Z/…).
 * @var ThreadInfo::cpu_percent Thread's share of CPU time in [0.0, 100.0].
 */
typedef struct {
    int    tid;
    char   state;
    double cpu_percent;
} ThreadInfo;

/**
 * @brief Per-process statistics for one sampling interval.
 *
 * @var ProcInfo::pid          Process ID.
 * @var ProcInfo::name         Executable name (truncated to MAX_NAME − 1 chars).
 * @var ProcInfo::state        Single-character process state (R/S/D/Z/…).
 * @var ProcInfo::mem_kb       Resident set size in kibibytes (VmRSS).
 * @var ProcInfo::cpu_percent  Aggregate CPU usage of this process [0.0, 100.0].
 * @var ProcInfo::priority     Kernel scheduling priority (field 18 of /proc/pid/stat).
 * @var ProcInfo::nice         Nice value in the range [-20, 19].
 * @var ProcInfo::sched_policy Linux scheduling policy (SCHED_OTHER=0, SCHED_FIFO=1, …).
 * @var ProcInfo::rt_priority  Real-time priority; 0 for non-RT processes.
 * @var ProcInfo::num_threads  Number of threads in the process.
 * @var ProcInfo::threads      Per-thread stats for up to MAX_THREADS threads.
 */
typedef struct {
    int        pid;
    char       name[MAX_NAME];
    char       state;
    long       mem_kb;
    double     cpu_percent;
    int        priority;
    int        nice;
    int        sched_policy;
    int        rt_priority;
    int        num_threads;
    ThreadInfo threads[MAX_THREADS];
} ProcInfo;

/**
 * @brief Ordered list of process snapshots for one sampling interval.
 *
 * @var ProcList::procs  Array of per-process snapshots, sorted by descending CPU%.
 * @var ProcList::count  Number of valid entries in procs[].
 */
typedef struct {
    ProcInfo procs[MAX_PROCS];
    int      count;
} ProcList;

/**
 * @brief Record baseline tick counters for all running processes.
 *
 * Must be called before the first proc_get_list() so that meaningful
 * CPU deltas can be computed.  Scans /proc and stores utime + stime for
 * every visible PID and each of its threads.
 */
void proc_sample(void);

/**
 * @brief Build a sorted process list with CPU and memory usage.
 *
 * Reads current tick counters from /proc, diffs against the baseline
 * recorded by the last proc_sample() call, and fills @p list.  The
 * resulting array is sorted by descending cpu_percent.
 *
 * @param list  Output list to populate; count is set to the number of
 *              processes successfully read.
 */
void proc_get_list(ProcList *list);

#endif /* PROCESS_H */
