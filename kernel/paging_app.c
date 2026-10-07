#include "paging_app.h"
#include "appmem.h"
#include "pgalloc.h"
#include "kstring.h"
#include "io.h"

u32 paging_app_bad_free_count;
u32 paging_app_pt_nospc_count, paging_app_data_nospc_count;

int paging_app_context(const struct addrspace *as)
{
    if (!as || as->appmem_poisoned || !as->pd_phys || as->pd_phys % PAGE_SIZE || !as->lease_pt_phys[0] ||
        as->app_pde != APP_BAND_PDE || as->app_pde_count != MEM_APP_BAND_MAX_PDES ||
        !pgalloc_page_owned(as->pd_phys / PAGE_SIZE, as->owner) ||
        (paging_current_cr3() != paging_kernel_pd_phys() &&
         paging_current_cr3() != as->pd_phys) || kctx_irq_depth || kctx_exc_depth ||
        !_irq_enabled()) return 0;
    const u32 *pd = P2V(as->pd_phys);
    for (u32 k = 0; k < MEM_APP_BAND_MAX_PDES; k++) {
        u32 phys = as->app_pt_phys[k], d = pd[APP_BAND_PDE + k];
        if (!phys) { if (d) return 0; continue; }
        if (phys % PAGE_SIZE || !pgalloc_page_owned(phys / PAGE_SIZE, as->owner) ||
            (d & ~(PTE_USER | PTE_ACCESSED | PTE_DIRTY)) != (phys | PAGE_RW))
            return 0;
    }
    return 1;
}

static u32 *app_entry(const struct paging_app_stage *tx, u32 va)
{
    u32 k = (va - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    u32 phys = tx->pending[k] ? tx->pending[k] : tx->as->app_pt_phys[k];
    return phys ? &((u32 *)P2V(phys))[(va >> PAGE_SHIFT) % PTE_COUNT] : 0;
}

static void app_return(struct addrspace *as, u32 phys)
{
    if (as->appmem_poisoned) return;
    if (!pgalloc_free_n_owner(as->owner, phys / PAGE_SIZE, 1)) {
        paging_app_bad_free_count++;
        paging_app_poison(as);
    }
}

void paging_app_abort(struct paging_app_stage *tx)
{
    /* Data before PT: their return list is held only in these NP entries. */
    for (u32 va = tx->base; va < tx->end; va += PAGE_SIZE) {
        u32 *entry = app_entry(tx, va);
        if (entry && *entry) {
            app_return(tx->as, *entry & ~(PAGE_SIZE - 1U));
            *entry = 0;
        }
    }
    for (u32 k = 0; k < MEM_APP_BAND_MAX_PDES; k++) if (tx->pending[k]) {
        app_return(tx->as, tx->pending[k]);
        tx->pending[k] = 0;
    }
}

int paging_app_stage(struct paging_app_stage *tx, struct addrspace *as,
                     u32 base, u32 end)
{
    if (!tx || !paging_app_context(as) || base < MEM_EXEC_LOAD_ADDR ||
        end > MEM_APP_BAND_MAX_TOP || base >= end ||
        (base % PAGE_SIZE) || (end % PAGE_SIZE)) return APPMEM_EINVAL;
    kmemset(tx, 0, sizeof(*tx));
    tx->as = as; tx->base = base; tx->end = end;
    /* Detect extent/PTE disagreement before the first allocation or write. */
    for (u32 va = base; va < end; va += PAGE_SIZE) {
        u32 *entry = app_entry(tx, va);
        if (entry && *entry) return APPMEM_EINVAL;
    }
    u32 first = (base - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    u32 last = (end - 1 - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    for (u32 k = first; k <= last; k++) if (!as->app_pt_phys[k]) {
        tx->pending[k] = pgalloc_alloc_phys(as->owner, 1);
        if (!tx->pending[k]) {
            paging_app_pt_nospc_count++;
            goto nospc;
        }
        kmemset(P2V(tx->pending[k]), 0, PAGE_SIZE);
    }
    for (u32 va = base; va < end; va += PAGE_SIZE) {
        u32 phys = pgalloc_alloc_phys(as->owner, 1);
        if (!phys) {
            paging_app_data_nospc_count++;
            goto nospc;
        }
        kmemset(P2V(phys), 0, PAGE_SIZE);
        *app_entry(tx, va) = phys | PTE_RW | PTE_USER;
    }
    /* Pending PTs are unreachable until the PDE is published. */
    for (u32 va = base; va < end; va += PAGE_SIZE) {
        u32 k = (va - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
        if (tx->pending[k]) *app_entry(tx, va) |= PTE_PRESENT;
    }
    return 0;
nospc:
    paging_app_abort(tx);
    return APPMEM_ENOSPC;
}

void paging_app_commit(struct paging_app_stage *tx)
{
    struct addrspace *as = tx->as;
    u32 *pd = P2V(as->pd_phys);
    u32 first = (tx->base - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    u32 last = (tx->end - 1 - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    for (u32 k = first; k <= last; k++) if (!tx->pending[k]) {
        u32 lo = MEM_APP_BAND_BASE + k * MEM_APP_BAND_PDE_SIZE;
        u32 hi = lo + MEM_APP_BAND_PDE_SIZE;
        if (lo < tx->base) lo = tx->base;
        if (hi > tx->end) hi = tx->end;
        for (u32 va = lo; va < hi; va += PAGE_SIZE) *app_entry(tx, va) |= PTE_PRESENT;
    }
    for (u32 k = first; k <= last; k++) {
        if (tx->pending[k]) pd[APP_BAND_PDE + k] = tx->pending[k] | PAGE_RW | PTE_USER;
        else pd[APP_BAND_PDE + k] |= PTE_USER;
    }
    for (u32 k = first; k <= last; k++) if (tx->pending[k]) {
        as->app_pt_phys[k] = tx->pending[k];
        tx->pending[k] = 0;
    }
    tx->end = tx->base; /* Committed data no longer belongs to abort. */
}

u32 paging_app_unmap_reject_count;

static int app_releasable(u32 owner, u32 phys)
{
    u32 pfn = phys / PAGE_SIZE;
    if (!pgalloc_page_owned(pfn, owner)) return 0;
    /* Match pgalloc_free_n_owner's live SURFACE predicate before withdrawal. */
    for (u32 i = 0; i < LEDGER_MAX_SURFACES; i++) {
        const struct ledger_surface *sf = &ledger_surfaces[i];
        if (sf->npages && (!sf->closing || sf->lease_count) &&
            pfn >= sf->first && pfn - sf->first < sf->npages) return 0;
    }
    return 1;
}

static int unmap_empty(const struct paging_app_unmap *tx, u32 k)
{
    return k < MEM_APP_BAND_MAX_PDES / 2 ?
        !!(tx->empty_lo & (1U << k)) :
        !!(tx->empty_hi & (1U << (k - MEM_APP_BAND_MAX_PDES / 2)));
}

static u32 *unmap_entry(const struct paging_app_unmap *tx, u32 va)
{
    u32 k = (va - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    return &((u32 *)P2V(tx->as->app_pt_phys[k]))[(va >> PAGE_SHIFT) % PTE_COUNT];
}

int paging_app_unmap_prepare(struct paging_app_unmap *tx, struct addrspace *as,
                             u32 base, u32 end)
{
    struct paging_app_unmap plan = {as, base, end, 0, 0};
    if (!tx || !paging_app_context(as) || base < MEM_EXEC_LOAD_ADDR ||
        end > MEM_APP_BAND_MAX_TOP || base >= end || base % PAGE_SIZE || end % PAGE_SIZE)
        goto reject;
    for (u32 va = base; va < end; va += PAGE_SIZE) {
        u32 k = (va - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
        if (!as->app_pt_phys[k]) goto reject;
        u32 e = *unmap_entry(&plan, va);
        if ((e & (PAGE_SIZE - 1U) & ~(PTE_ACCESSED | PTE_DIRTY)) != (PAGE_RW | PTE_USER) ||
            !app_releasable(as->owner, e & ~(PAGE_SIZE - 1U))) goto reject;
    }
    u32 first = (base - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    u32 last = (end - 1 - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    for (u32 k = first; k <= last; k++) {
        if (!k) continue; /* shlib.c requires app_pt_phys[0] even when empty. */
        u32 *pt = P2V(as->app_pt_phys[k]);
        int empty = 1;
        for (u32 j = 0; j < PTE_COUNT; j++) {
            u32 va = MEM_APP_BAND_BASE + k * MEM_APP_BAND_PDE_SIZE + j * PAGE_SIZE;
            if ((va < base || va >= end) && pt[j]) { empty = 0; break; }
        }
        if (empty) {
            if (!app_releasable(as->owner, as->app_pt_phys[k])) goto reject;
            if (k < MEM_APP_BAND_MAX_PDES / 2) plan.empty_lo |= 1U << k;
            else plan.empty_hi |= 1U << (k - MEM_APP_BAND_MAX_PDES / 2);
        }
    }
    *tx = plan;
    return 0;
reject:
    paging_app_unmap_reject_count++;
    return APPMEM_EINVAL;
}

void paging_app_unmap_withdraw(struct paging_app_unmap *tx)
{
    u32 first = (tx->base - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    u32 last = (tx->end - 1 - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    u32 *pd = P2V(tx->as->pd_phys);
    /* Empty PTs become unreachable via PDE=0. Leave their PTEs untouched
     * until CR3 synchronization, avoiding O(all data pages) IRQ work. */
    for (u32 k = first; k <= last; k++) {
        if (unmap_empty(tx, k)) { pd[APP_BAND_PDE + k] = 0; continue; }
        u32 lo = MEM_APP_BAND_BASE + k * MEM_APP_BAND_PDE_SIZE;
        u32 hi = lo + MEM_APP_BAND_PDE_SIZE;
        if (lo < tx->base) lo = tx->base;
        if (hi > tx->end) hi = tx->end;
        for (u32 va = lo; va < hi; va += PAGE_SIZE) *unmap_entry(tx, va) &= ~PTE_PRESENT;
    }
    if (paging_current_cr3() == tx->as->pd_phys) paging_load_cr3(tx->as->pd_phys);
}

int paging_app_unmap_free(struct paging_app_unmap *tx)
{
    /* Now even the withdrawn PT entries can be changed, with IF=1. */
    for (u32 va = tx->base; va < tx->end; va += PAGE_SIZE)
        *unmap_entry(tx, va) &= ~PTE_PRESENT;
    for (u32 va = tx->base; va < tx->end; va += PAGE_SIZE) {
        u32 *entry = unmap_entry(tx, va);
        if (!pgalloc_free_n_owner(tx->as->owner, *entry / PAGE_SIZE, 1)) {
            paging_app_bad_free_count++;
            paging_app_poison(tx->as);
            return APPMEM_EINVAL; /* Retain the failed frame for diagnosis. */
        }
        *entry = 0;
    }
    u32 first = (tx->base - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    u32 last = (tx->end - 1 - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    for (u32 k = first; k <= last; k++) if (unmap_empty(tx, k)) {
        if (!pgalloc_free_n_owner(tx->as->owner, tx->as->app_pt_phys[k] / PAGE_SIZE, 1)) {
            paging_app_bad_free_count++;
            paging_app_poison(tx->as);
            return APPMEM_EINVAL;
        }
        tx->as->app_pt_phys[k] = 0;
    }
    return 0;
}

void paging_app_poison(struct addrspace *as)
{
    paging_addrspace_poison(as);
}
