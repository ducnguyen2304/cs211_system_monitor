#ifndef PROCESS_H
#define PROCESS_H

#define MAX_PROCS   512
#define MAX_THREADS  64
#define MAX_NAME     64

typedef struct {
    int    tid;
    char   state;
    double cpu_percent;
} ThreadInfo;

typedef struct {
    int        pid;
    char       name[MAX_NAME];
    char       state;
    long       mem_kb;
    double     cpu_percent;
    /* scheduling info (feature 2) */
    int        priority;      /* kernel priority, field 18 of /proc/pid/stat */
    int        nice;          /* nice value, -20..19 */
    int        sched_policy;  /* SCHED_OTHER=0, SCHED_FIFO=1, SCHED_RR=2, etc. */
    int        rt_priority;   /* real-time priority (0 for normal processes) */
    /* per-thread stats (feature 1) */
    int        num_threads;
    ThreadInfo threads[MAX_THREADS];
} ProcInfo;

typedef struct {
    ProcInfo procs[MAX_PROCS];
    int      count;
} ProcList;

/* Call proc_sample() once as baseline; proc_get_list() computes deltas. */
void proc_sample(void);
void proc_get_list(ProcList *list);

#endif
