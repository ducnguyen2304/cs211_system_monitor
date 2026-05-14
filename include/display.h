#ifndef DISPLAY_H
#define DISPLAY_H

#include "cpu.h"
#include "memory.h"
#include "process.h"

void display_init(void);
void display_update(const CpuInfo *cpu, const MemInfo *mem, const ProcList *procs);
void display_cleanup(void);

#endif
