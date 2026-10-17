#include "kselftest.h"
#include "memory_boot.h"
#include "module.h"
#include "paging.h"
#include "memmap.h"
#include "kstring.h"
#include "kprintf.h"

extern u32 audit_runs, audit_fail;
extern char audit_first_fail_tag[32];
u32 boot_pages_audit_runs, boot_pages_audit_fail;

/* Once, after the debug body and gfx backing have acquired their pages. */
void kaudit_boot_pages(void)
{
    struct memory_page_counts c;
    memory_boot_page_counts(kdebug.pages ? V2P((void *)(uptr)kdebug.base) / PAGE_SIZE : 0,
                            kdebug.pages, &c);
    boot_pages_audit_runs++;
    boot_pages_audit_fail += c.diff != 0;
    audit_runs++;
    if (c.diff && !audit_fail)
        kstrncpy(audit_first_fail_tag, "boot-pages", sizeof(audit_first_fail_tag));
    audit_fail += c.diff != 0;
    kprintf(0x07, "[mem] pages fixed=%u slack=%u pool=%u meta=%u bb=%u kdebug=%u other=%u total=%u phys=%u %s diff=%d\n",
            c.fixed, c.slack, c.pool, c.meta, c.bb, c.kdebug, c.other,
            c.total, c.phys, c.diff ? "ng" : "ok", c.diff);
}
