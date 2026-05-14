#include "memory.h"
#include <stdio.h>
#include <string.h>

void mem_get_info(MemInfo *info) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;

    long swap_free = 0;
    char key[64];
    long value;
    char unit[16];

    while (fscanf(f, "%63s %ld %15s\n", key, &value, unit) >= 2) {
        if      (strcmp(key, "MemTotal:")     == 0) info->total_kb     = value;
        else if (strcmp(key, "MemFree:")      == 0) info->free_kb       = value;
        else if (strcmp(key, "MemAvailable:") == 0) info->available_kb  = value;
        else if (strcmp(key, "Buffers:")      == 0) info->buffers_kb    = value;
        else if (strcmp(key, "Cached:")       == 0) info->cached_kb     = value;
        else if (strcmp(key, "SwapTotal:")    == 0) info->swap_total_kb = value;
        else if (strcmp(key, "SwapFree:")     == 0) swap_free           = value;
    }
    fclose(f);

    info->used_kb     = info->total_kb - info->available_kb;
    info->swap_used_kb = info->swap_total_kb - swap_free;
    info->used_percent = info->total_kb > 0
                         ? (double)info->used_kb / info->total_kb * 100.0
                         : 0.0;
}
