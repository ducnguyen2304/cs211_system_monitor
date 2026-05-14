#ifndef CPU_H
#define CPU_H

#define MAX_CORES 64

typedef struct {
    int    num_cores;
    double usage[MAX_CORES];   /* per-core usage 0.0–100.0 */
    double total_usage;
} CpuInfo;

/* Call twice with a sleep in between; second call returns valid usage. */
void cpu_sample(void);
void cpu_get_info(CpuInfo *info);

#endif
