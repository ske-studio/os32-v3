#ifndef PLATFORM_MEM_STAT_H
#define PLATFORM_MEM_STAT_H

#include "os32_kapi_shared.h"

static inline void platform_mem_stat_pre(MemStat *snap, u32 size)
{
    (void)snap;
    (void)size;
}

static inline void platform_mem_stat_diag(MemStat *snap)
{
    (void)snap;
}

#endif
