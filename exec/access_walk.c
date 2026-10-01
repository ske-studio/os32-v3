/* B1 managed USER page walk. Caller holds IRQs and has resolved a live AS.
 * T2c keeps table/data backing in the supervisor identity map; no CR3 switch. */
#include "paging.h"
#include "pgalloc.h"
#include "shlib.h"
#include "exec.h"

int as_access_page(const struct addrspace *as, u32 va, int write, u32 *pa)
{
    u32 d, table, expected, pdi = va >> 22, result, frame, i;
    const u32 mask = ~(u32)(PAGE_SIZE - 1);
    if (!as->pd_phys || (as->pd_phys & ~mask) ||
        !pgalloc_page_owned(as->pd_phys / PAGE_SIZE, as->owner)) return 0;
    d = ((const u32 *)P2V(as->pd_phys))[pdi];
    if (d & PTE_PS) return 0;
    table = d & mask;
    if (va >= MEM_APP_BAND_BASE && va < MEM_APP_BAND_MAX_TOP)
        expected = as->app_pt_phys[pdi - APP_BAND_PDE];
    else if (va >= MEM_LEASE_BASE && va < MEM_LEASE_END)
        expected = as->lease_pt_phys[pdi - (MEM_LEASE_BASE >> 22)];
    else {
        expected = ((const u32 *)P2V(paging_kernel_pd_phys()))[pdi];
        if (!(expected & PTE_PRESENT) || (expected & PTE_PS)) return 0;
        expected &= mask;
        if (expected != paging_registered_pt(va)) return 0;
        goto shared;
    }
    if (!pgalloc_page_owned(expected / PAGE_SIZE, as->owner)) return 0;
shared:
    if (!expected || table != expected) return 0;
    if (write ? as_va_to_pa(as->pd_phys, va, &result) :
                as_va_to_pa_read(as->pd_phys, va, &result)) return 0;
    frame = result & mask;
    if (va >= MEM_LEASE_BASE && va < MEM_LEASE_END) {
        for (i = 0; i < MEM_LEASE_MAX; i++) {
            const struct as_lease *l = &as->leases[i];
            const struct ledger_surface *sf;
            if (!l->token || va < l->base ||
                (va - l->base) / PAGE_SIZE >= l->npages) continue;
            if (l->sid >= LEDGER_MAX_SURFACES) return 0;
            sf = &ledger_surfaces[l->sid];
            if (sf->gen != l->generation || !sf->lease_count ||
                sf->npages != l->npages ||
                (sf->backing != LEDGER_SB_RAM && sf->backing != LEDGER_SB_FIXED_RAM) ||
                sf->perm_max == LEDGER_PERM_NONE ||
                (write && (!(l->flags & PTE_RW) || sf->perm_max != LEDGER_PERM_RW)) ||
                !ledger_surface_validate(sf) ||
                frame != (sf->first + (va - l->base) / PAGE_SIZE) * PAGE_SIZE)
                return 0;
            goto allowed;
        }
        return 0;
    }
    if (va >= MEM_APP_BAND_BASE && va < MEM_APP_BAND_MAX_TOP) {
        if (pgalloc_page_owned(frame / PAGE_SIZE, as->owner)) {
            /* Table frames are never payload, even under the same owner. */
            if (frame == as->pd_phys) return 0;
            for (i = 0; i < MEM_APP_BAND_MAX_PDES; i++)
                if (frame == as->app_pt_phys[i]) return 0;
            for (i = 0; i < MEM_LEASE_MAX_PDES; i++)
                if (frame == as->lease_pt_phys[i]) return 0;
            goto allowed;
        }
        if (!write && shlib_read_page(va, frame)) goto allowed;
        return 0;
    }
    if (result != va) return 0;
    if (va >= (u32)MEM_SHM_BASE && va - (u32)MEM_SHM_BASE < MEM_SHM_SIZE)
        goto allowed;
    if (!write && frame && frame == exec_tramp_page_addr() &&
        !(((const u32 *)P2V(table))[(va >> PAGE_SHIFT) % PTE_COUNT] & PTE_RW))
        goto allowed;
    return 0;
allowed:
    *pa = result;
    return 1;
}
