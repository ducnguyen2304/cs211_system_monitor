/**
 * @file cpu.h
 * @brief CPU usage sampling interface.
 *
 * Provides a two-call API for computing CPU utilisation across all logical
 * cores by diffing successive /proc/stat snapshots.
 *
 * Typical usage:
 *   cpu_sample();          // establish baseline
 *   sleep(1);
 *   cpu_get_info(&info);   // compute delta and fill CpuInfo
 */

#ifndef CPU_H
#define CPU_H

/** Maximum number of logical CPU cores tracked. */
#define MAX_CORES 64

/**
 * @brief Snapshot of CPU usage across all cores.
 *
 * @var CpuInfo::num_cores   Number of logical cores detected in /proc/stat.
 * @var CpuInfo::usage       Per-core utilisation in the range [0.0, 100.0].
 * @var CpuInfo::total_usage Aggregate utilisation across all cores [0.0, 100.0].
 */
typedef struct {
    int    num_cores;
    double usage[MAX_CORES];
    double total_usage;
} CpuInfo;

/**
 * @brief Record a baseline CPU tick snapshot.
 *
 * Must be called before the first cpu_get_info() so a meaningful delta
 * can be computed.  Subsequent calls update the stored baseline without
 * returning any data.
 */
void cpu_sample(void);

/**
 * @brief Compute CPU utilisation since the last cpu_sample() call.
 *
 * Reads /proc/stat, diffs against the internally stored baseline, and
 * writes results into @p info.  Also advances the baseline so repeated
 * calls without an intervening cpu_sample() produce correct output.
 *
 * @param info  Output struct populated with per-core and aggregate usage.
 */
void cpu_get_info(CpuInfo *info);

#endif /* CPU_H */
