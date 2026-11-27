/* X1 runs the candidate's teardown body with the same AS used by C4/C7/C15.
 * Keep processor page-table destruction and non-graphics resources in the fake. */
#include "sys/kern/exec/appslot.h"
#include "sys/hal/gfx/v2/gfx2_exec.h"

extern u32 gfx2_leak_pages;
static void console_printf(u8 attr, const char *fmt, ...) {}
static u32 exec_as_leftover_pages, destroys, reclaims, poisoned, freed_as;
static void exec_serial_puts_polled(const char *s) {}
static void exec_owner_diag(AppSlot *slot, int exiting, u32 pages, u32 leftover) {}
static void exec_shlib_addrspace_detach(struct addrspace *as) {}
static int exec_gfx_owner_release(struct addrspace *as) { return gfx2_owner_release(as); }
int lease_revoke_all(struct addrspace *as) { return 0; }
void paging_addrspace_poison(struct addrspace *as) { as->appmem_poisoned=1; poisoned++; }
u32 paging_addrspace_free_user_range(struct addrspace *as, u32 start, u32 end) { return 0; }
void paging_addrspace_destroy(struct addrspace *as) { destroys++; }
void kfree(void *ptr) { if (ptr==&app) freed_as++; }
u32 ledger_owner_pages(u32 owner)
{
    u32 n=0;
    for (u32 i=0;i<PAGES;i++) if (owners[i]==owner) n++;
    return n;
}
int ledger_reclaim_owner(u32 owner, u32 *pages)
{
    reclaims++;
    *pages=ledger_owner_pages(owner);
    return 1;
}
int ledger_owner_retire(u32 owner) { return 1; }
int pgalloc_pinned(u32 pfn) { u32 i=pfn-PHYS/PAGE_SIZE; return i<PAGES ? pins[i] : 0; }
int pgalloc_page_owned(u32 pfn, u32 owner) { u32 i=pfn-PHYS/PAGE_SIZE; return i<PAGES && owners[i]==owner; }
int ledger_transfer_pinned(u32 pfn, int n, u32 from, u32 to)
{
    u32 i=pfn-PHYS/PAGE_SIZE;
    if (n!=1 || i>=PAGES || owners[i]!=from || !pins[i]) return 0;
    owners[i]=to;
    return 1;
}

#include GFX2_TEARDOWN_BODY

static int teardown(struct gfx2_adapter *a)
{
    for (int fail=0;fail<=1;fail++) {
        AppSlot slot={.cpl3=1,.as=&app};
        u32 sid, pitch;
        void *va;
        if (gfx2_surface_create(a->index,64,128,GFX2_FMT_INDEX8,GFX2_POOL_SYSMEM,
                                GFX2_USE_RENDER2D,&sid) ||
            gfx2_lock(sid,0,&va,&pitch) || pitch!=64 || ledger_owner_pages(app.owner)!=2)
            return 13;
        ((u8 *)va)[PAGE_SIZE]=0x69;
        inject_unmap=fail;
        exec_teardown_app(&slot);
        struct gfx2_surf *s=&gfx2_surfs[sid%GFX2_MAX_SURFACES];
        if (slot.as || slot.cpl3 || s->map_as || freed_as!=(u32)fail+1) return 13;
        if (!fail) {
            if (s->state!=GFX2_ST_FREE || ledger_owner_pages(app.owner) ||
                exec_as_leftover_pages || destroys!=1 || reclaims!=1 || poisoned) return 13;
        } else {
            if (!app.appmem_poisoned || poisoned!=1 || destroys!=1 || reclaims!=1 ||
                exec_as_leftover_pages!=2 || ledger_owner_pages(app.owner)!=2 ||
                s->state!=GFX2_ST_QUAR || !(s->flags&GFX2_SF_KEEP)) return 14;
            kmemset(&app,0xcc,sizeof(app));
            gfx2_closing_sweep(a);
            gfx2_quarantine_drain(a);
            if (gfx2_leak_pages!=2 || ((u8 *)va)[PAGE_SIZE]!=0x69) return 14;
        }
    }
    return 0;
}
