#ifndef PLATFORM_MEM_STAT_H
#define PLATFORM_MEM_STAT_H

#include "os32_kapi_shared.h"
#include "kstack_hw.h"
#include "module.h"
#include "pgalloc.h"

extern u32 memmap_audit_runs, as_audit_runs, v86_return_audit_runs;
extern u32 memmap_audit_fail, as_audit_fail, v86_return_audit_fail;
extern u32 boot_pages_audit_runs, boot_pages_audit_fail;
extern int kselftest_pass, kselftest_fail;

static inline void platform_mem_stat_pre(MemStat *snap, u32 size)
{
    if (size > __builtin_offsetof(MemStat, kstack_high_water))
        snap->kstack_high_water = kstack_high_water();
}

static inline void platform_mem_stat_diag(MemStat *snap)
{
    snap->audit_runs = memmap_audit_runs + as_audit_runs + v86_return_audit_runs +
                      boot_pages_audit_runs;
    snap->audit_fail = memmap_audit_fail + as_audit_fail + v86_return_audit_fail +
                      boot_pages_audit_fail;
    snap->kselftest_pass = kselftest_pass;
    snap->kselftest_fail = kselftest_fail;
    snap->kdebug_state = kdebug.state;
    snap->kdebug_pair_crc = kdebug.pair_crc;
    snap->kdebug_pages = kdebug.pages;
    /* A rejected module keeps its retired owner number for diagnostics; an
     * app may already have reused that number. Only RUNNING owns these pages. */
    snap->kdebug_owner_pages = kdebug.state == KDEBUG_RUNNING && kdebug.owner ?
        ledger_owner_pages(kdebug.owner) : 0;
    snap->kdebug_open_count = kdebug.open_count;
}

#endif
