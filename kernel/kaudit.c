#include "kselftest.h"
#include "../mm/paging.h"
#include "v86/v86_mem.h"
#include "memmap.h"
#include "lease.h"
#include "exec.h"
#include "../mm/pgalloc.h"
#include "kstring.h"
#include "serial_diag.h"

/* Lifecycle diagnostics, intentionally externally visible in kernel.map. */
u32 memmap_audit_fail, as_audit_fail, memmap_audit_runs, as_audit_runs;
u32 memmap_audit_skip, v86_return_audit_fail, v86_return_audit_runs;
u32 audit_runs, audit_fail;
u32 v86_gcap_phase, v86_gcap_alloc_count, v86_gcap_free_count;
char audit_first_fail_tag[AUDIT_TAG_MAX];
int kselftest_run_audit(const char *tag)
{
    int a, b, bad;
    if (paging_v86_session_open()) { memmap_audit_skip++; return 0; }
    a = paging_master_audit(exec_tramp_page_addr());
    b = lease_audit_all();
    memmap_audit_runs++;
    as_audit_runs++;
    memmap_audit_fail += a != 0;
    as_audit_fail += b != 0;
    bad = (a != 0) + (b != 0) + !ledger_selfcheck(tag);
    audit_runs++;
    if (bad && !audit_fail) {
        kstrncpy(audit_first_fail_tag, tag ? tag : "unknown", sizeof(audit_first_fail_tag));
        audit_first_fail_tag[sizeof(audit_first_fail_tag) - 1] = 0;
        if (!serial_gate_active()) {
            serial_puts_polled("[audit] first failure: ");
            serial_puts_polled(audit_first_fail_tag);
            serial_puts_polled("\r\n");
        }
    }
    audit_fail += bad;
    return bad;
}

void kselftest_audit_v86_return(void)
{
    v86_return_audit_runs++;
    v86_return_audit_fail += kselftest_run_audit("v86-return") != 0;
}

/* Boot audit results are consumed by the optional tests without another scan. */
static int kaudit_last_rc[KAUDIT_TAG_COUNT];
static int kaudit_skipped[KAUDIT_TAG_COUNT];

void kaudit_post_exec(void)
{
    kaudit_skipped[KAUDIT_POST_EXEC] = paging_v86_session_open() != 0;
    kaudit_last_rc[KAUDIT_POST_EXEC] = kselftest_run_audit("post-exec");
    /* Keep the linker-derived address in the kernel; the optional selftest
     * consumes the same boot SHM permission observation through last_rc. */
    if (!kaudit_skipped[KAUDIT_POST_EXEC]) {
        u32 *pt = (u32 *)P2V(paging_registered_pt(MEM_SHM_BASE));
        kaudit_last_rc[KAUDIT_POST_EXEC_SHM] = !(pt &&
            (pt[(MEM_SHM_BASE >> PAGE_SHIFT) % PTE_COUNT] &
             (PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT)) == (PAGE_RW | PTE_USER));
    }
}

void kaudit_post_probe(void)
{
    kaudit_last_rc[KAUDIT_POST_PROBE] = kselftest_run_audit("post-probe");
}

int kdi_kaudit_last_rc(int tag)
{
    return tag >= 0 && tag < KAUDIT_TAG_COUNT ? kaudit_last_rc[tag] : -1;
}

int kdi_kaudit_skipped(int tag)
{
    return tag >= 0 && tag < KAUDIT_TAG_COUNT ? kaudit_skipped[tag] : 1;
}
