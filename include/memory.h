/**
 * @file memory.h
 * @brief RAM and swap memory statistics interface.
 *
 * Reads /proc/meminfo once per call and exposes a structured view of the
 * system's memory state, including utilisation percentage ready for display.
 */

#ifndef MEMORY_H
#define MEMORY_H

/**
 * @brief Snapshot of system memory and swap usage (all values in kibibytes).
 *
 * @var MemInfo::total_kb      Physical RAM installed.
 * @var MemInfo::used_kb       RAM in use (total − available).
 * @var MemInfo::free_kb       Completely unused RAM (MemFree).
 * @var MemInfo::available_kb  RAM available for new allocations without swapping.
 * @var MemInfo::buffers_kb    RAM used for kernel I/O buffers.
 * @var MemInfo::cached_kb     RAM used for the page/file cache.
 * @var MemInfo::swap_total_kb Total swap space configured.
 * @var MemInfo::swap_used_kb  Swap space currently in use (total − free).
 * @var MemInfo::used_percent  Percentage of total RAM that is used [0.0, 100.0].
 */
typedef struct {
    long   total_kb;
    long   used_kb;
    long   free_kb;
    long   available_kb;
    long   buffers_kb;
    long   cached_kb;
    long   swap_total_kb;
    long   swap_used_kb;
    double used_percent;
} MemInfo;

/**
 * @brief Populate @p info with current memory statistics.
 *
 * Parses /proc/meminfo and derives used_kb, swap_used_kb, and
 * used_percent from the raw kernel values.  Returns without modifying
 * @p info if /proc/meminfo cannot be opened.
 *
 * @param info  Output struct to fill.
 */
void mem_get_info(MemInfo *info);

#endif /* MEMORY_H */
