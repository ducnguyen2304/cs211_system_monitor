#ifndef MEMORY_H
#define MEMORY_H

typedef struct {
    long total_kb;
    long used_kb;
    long free_kb;
    long available_kb;
    long buffers_kb;
    long cached_kb;
    long swap_total_kb;
    long swap_used_kb;
    double used_percent;
} MemInfo;

void mem_get_info(MemInfo *info);

#endif
