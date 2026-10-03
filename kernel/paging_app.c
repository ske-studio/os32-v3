#include "paging_app.h"
#include "appmem.h"
#include "pgalloc.h"
#include "kstring.h"
#include "io.h"

u32 paging_app_bad_free_count;
u32 paging_app_pt_nospc_count, paging_app_data_nospc_count;

int paging_app_context(const struct addrspace *as)
{
    if (!as || !as->pd_phys || as->pd_phys % PAGE_SIZE || !as->lease_pt_phys[0] ||
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

static void app_return(u32 owner, u32 phys)
{
    if (!pgalloc_free_n_owner(owner, phys / PAGE_SIZE, 1)) paging_app_bad_free_count++;
}

void paging_app_abort(struct paging_app_stage *tx)
{
    /* Data before PT: their return list is held only in these NP entries. */
    for (u32 va = tx->base; va < tx->end; va += PAGE_SIZE) {
        u32 *entry = app_entry(tx, va);
        if (entry && *entry) {
            app_return(tx->as->owner, *entry & ~(PAGE_SIZE - 1U));
            *entry = 0;
        }
    }
    for (u32 k = 0; k < MEM_APP_BAND_MAX_PDES; k++) if (tx->pending[k]) {
        app_return(tx->as->owner, tx->pending[k]);
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
    return 0;
nospc:
    paging_app_abort(tx);
    return APPMEM_ENOSPC;
}

void paging_app_commit(struct paging_app_stage *tx)
{
    struct addrspace *as = tx->as;
    u32 *pd = P2V(as->pd_phys);
    for (u32 va = tx->base; va < tx->end; va += PAGE_SIZE)
        *app_entry(tx, va) |= PTE_PRESENT;
    u32 first = (tx->base - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    u32 last = (tx->end - 1 - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    for (u32 k = first; k <= last; k++) {
        if (tx->pending[k]) pd[APP_BAND_PDE + k] = tx->pending[k] | PAGE_RW | PTE_USER;
        else pd[APP_BAND_PDE + k] |= PTE_USER;
    }
    for (u32 k = first; k <= last; k++) if (tx->pending[k]) {
        as->app_pt_phys[k] = tx->pending[k];
        tx->pending[k] = 0;
    }
}
