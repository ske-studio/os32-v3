/* ======================================================================== */
/*  kapi_sys.c — KernelAPI システム情報関数                                  */
/* ======================================================================== */

#include "os32_kapi_shared.h"
#include "lib/kstring.h"
#include "exec.h"             /* CPL=3 ポインタ検証 */
#include "appslot.h"
#include "redir_access.h"
#include "exec_heap.h"
#include "kmalloc.h"
#include "pgalloc.h"
#include "io.h"
#include "kstack_hw.h"
#include "module.h"
#include "ver_info.h"
#include "kapi_sysinfo.h"
extern u32 exec_as_leftover_pages;
extern volatile u32 fault_kill_count;
extern u32 memmap_audit_runs, as_audit_runs, v86_return_audit_runs;
extern u32 memmap_audit_fail, as_audit_fail, v86_return_audit_fail;
extern u32 boot_pages_audit_runs, boot_pages_audit_fail;
extern int kselftest_pass, kselftest_fail;

extern volatile int ring3_in_syscall;

i32 kapi_mem_stat(i32 app_id, void *out, u32 size) __attribute__((cold));

STATIC_ASSERT(sizeof(MemStat) == 220, memstat_size);
STATIC_ASSERT(__builtin_offsetof(MemStat, kstack_high_water) == 132, memstat_v73_prefix);
STATIC_ASSERT(__builtin_offsetof(MemStat, kdebug_state) == 200, memstat_v74_prefix);
STATIC_ASSERT(__builtin_offsetof(MemStat, owner_pages) == 196, memstat_owner_pages);
STATIC_ASSERT(__builtin_offsetof(MemStat, extents_total) == MEMSTAT_MIN, memstat_min);
STATIC_ASSERT(__builtin_offsetof(MemStat, img_end) == 80, memstat_layout);
STATIC_ASSERT(__builtin_offsetof(MemStat, load_addr) == 96, memstat_slot);
STATIC_ASSERT(__builtin_offsetof(MemStat, pressure_epoch) == 120, memstat_pressure_epoch);
STATIC_ASSERT(__builtin_offsetof(MemStat, trim_pending_mask) == 124, memstat_trim_pending_mask);
STATIC_ASSERT(__builtin_offsetof(MemStat, trim_epoch) == 128, memstat_trim_epoch);

/* The generated wrapper validates the entire output first. Snapshot and
 * copyout never yield, call USER code, or switch AS. Only snapshot holds IF. */
i32 kapi_mem_stat(i32 app_id, void *out, u32 size)
{
    MemStat snap = {0};
    struct caller_access caller;
    AppSlot *slot;
    unsigned int irq;
    int result = OS32_ERR_INVAL;

    if (!out || size < MEMSTAT_MIN || app_id < -1 || app_id > APP_ID_MAX ||
        app_id == APP_ID_SHELL) return OS32_ERR_INVAL;
    if (size > __builtin_offsetof(MemStat, kstack_high_water))
        snap.kstack_high_water = kstack_high_water();
    snap.kheap_total = kmalloc_total();
    snap.kheap_used = kmalloc_used();
    snap.kheap_free = kmalloc_free();
    irq = irq_save();
    if (ring3_wm_depth > 0) {
        if (app_id == -1) app_id = 0;
    } else if (!ring3_in_syscall) {
        slot = appslot_get(appslot_cur());
        if (appslot_cur() != APP_ID_SHELL || !slot || slot->cpl3) goto done;
        if (app_id == -1) app_id = 0;
    } else {
        if (!caller_access_get_user(&caller)) goto done;
        if (app_id == -1) app_id = caller.app_id;
        if (app_id != 0 && app_id != caller.app_id) goto done;
    }
    if (app_id) {
        struct addrspace *as;
        slot = appslot_get(app_id);
        result = OS32_ERR_NOTFOUND;
        if (!slot || !(as = slot->as) || !as->pd_phys) goto done;
        snap.owner_pages = ledger_owner_pages(as->owner);
        snap.state = slot->state;
        snap.flags = MEMSTAT_HAS_AS;
        if (slot->trim_pending) snap.flags |= MEMSTAT_TRIM_PENDING;
        snap.trim_epoch = slot->trim_epoch;
        if (as->appmem_poisoned) snap.flags |= MEMSTAT_POISONED;
        if (as->exec_heap_used == ~(u32)0) snap.flags |= MEMSTAT_HEAP_INVALID;
        else snap.exec_heap_used = as->exec_heap_used;
        for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) {
            u32 kind = as->appmem.e[i].kind;
            if (kind >= APPMEM_LIBC_INITIAL && kind <= APPMEM_EXEC_LARGE) {
                snap.extents_total++;
                snap.extents[kind - 1]++;
            }
        }
        snap.extents_free = APPMEM_EXTENT_MAX - snap.extents_total;
        snap.arenas = snap.extents[APPMEM_ANON - 1] + snap.extents[APPMEM_EXEC_ARENA - 1];
        snap.img_end = as->appmem_layout.img_end;
        snap.primary_mapped_end = as->appmem_layout.primary_mapped_end;
        snap.exec_heap_cur_end = as->appmem_layout.exec_heap_cur_end;
        snap.guard_b = as->appmem_layout.guard_b;
        snap.load_addr = slot->load_addr;
        snap.sbrk_heap_limit = slot->sbrk_heap_limit;
        snap.exec_heap_base = slot->exec_heap_base;
        snap.exec_heap_size = slot->exec_heap_size;
        snap.stack_top = slot->stack_top;
        snap.stack_size = slot->stack_size;
    }
    snap.pressure_epoch = appslot_trim_epoch;
    for (int id = APP_ID_MIN; id <= APP_ID_MAX; id++) {
        slot = appslot_at(id);
        if (slot->trim_pending) snap.trim_pending_mask |= 1UL << id;
        if (slot->state != APP_STATE_FREE && slot->as)
            for (u32 i = 0; i < MEM_LEASE_MAX; i++)
                snap.lease_active += slot->as->leases[i].token != 0;
    }
    snap.kheap_peak = kmalloc_peak_bytes;
    snap.resident_heap_peak = resident_heap_peak;
    snap.resident_heap_fail = resident_heap_fail;
    snap.leftover_pages = exec_as_leftover_pages;
    snap.ledger_irq_ops = ledger_irq_ops;
    snap.ledger_exc_ops = ledger_exc_ops;
    snap.ledger_bad_free = ledger_bad_free;
    snap.fault_kill_count = fault_kill_count;
    snap.reclaim_count = appslot_reclaim_count;
    snap.stop_park_count = ring3_stop_park_count;
    snap.audit_runs = memmap_audit_runs + as_audit_runs + v86_return_audit_runs +
                      boot_pages_audit_runs;
    snap.audit_fail = memmap_audit_fail + as_audit_fail + v86_return_audit_fail +
                      boot_pages_audit_fail;
    snap.kselftest_pass = kselftest_pass;
    snap.kselftest_fail = kselftest_fail;
    snap.kdebug_state = kdebug.state;
    snap.kdebug_pair_crc = kdebug.pair_crc;
    snap.kdebug_pages = kdebug.pages;
    /* A rejected module keeps its retired owner number for diagnostics; an
     * app may already have reused that number. Only RUNNING owns these pages. */
    snap.kdebug_owner_pages = kdebug.state == KDEBUG_RUNNING && kdebug.owner ?
        ledger_owner_pages(kdebug.owner) : 0;
    snap.kdebug_open_count = kdebug.open_count;
    snap.size = size < sizeof(snap) ? size : sizeof(snap);
    snap.app_id = app_id;
    snap.phys_total_pages = pgalloc_total_pages();
    snap.phys_free_pages = pgalloc_free_pages();
    snap.resident_heap_total = exec_heap_total();
    snap.resident_heap_used = exec_heap_used();
    result = (int)snap.size;
done:
    irq_restore(irq);
    if (result > 0) {
        kmemcpy(out, &snap, snap.size);
    }
    return result;
}

/* The generated __cdecl wrapper supplies the existing output-range check. */
int __cdecl kapi_sys_ver_info(void *out, u32 size)
{
    VerSnapshot s;

    if (!out || size != sizeof(OS32_VerInfo)) return OS32_ERR_INVAL;
    ver_collect(&s);
    kapi_sys_build_time_copy(s.build, sizeof(s.build));
    ver_format(&s, (OS32_VerInfo *)out);
    return 0;
}
