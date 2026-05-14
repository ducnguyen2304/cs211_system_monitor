#ifndef PROCESS_H
#define PROCESS_H

#define MAX_PROCS 512
#define MAX_NAME  64

typedef struct {
    int    pid;
    char   name[MAX_NAME];
    char   state;
    long   mem_kb;       /* VmRSS — resident set size */
    double cpu_percent;
} ProcInfo;

typedef struct {
    ProcInfo procs[MAX_PROCS];
    int      count;
} ProcList;

/* Call twice with a sleep; second call populates cpu_percent. */
void proc_sample(void);
void proc_get_list(ProcList *list);

#endif
