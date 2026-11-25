/* Included by the master mapping implementation. Only an empty, dedicated
 * PDE can become ours. Release is for rollback before any child exists. */
#include "hwb_window.h"
static u32 *window_table;

int paging_window_reserve(void)
{
    u32 di = PDE_INDEX(PPC_HWB_WINDOW_VA);
    if (window_table || page_tables[di] || page_directory[di] ||
        prepare_tables(PPC_HWB_WINDOW_VA >> PAGE_SHIFT, 2))
        return -1;
    window_table = page_tables[di];
    arch_mmu_flush_tlb();
    return 0;
}

int paging_window_release(void)
{
    u32 di = PDE_INDEX(PPC_HWB_WINDOW_VA);
    if (!window_table || live_addrspaces || page_tables[di] != window_table ||
        (page_directory[di] & PTE_FRAME_MASK) != V2P(window_table) ||
        paging_current_cr3() != paging_kernel_pd_phys())
        return -1;
    page_directory[di] = 0;
    page_tables[di] = 0;
    arch_mmu_flush_tlb();
    pgalloc_free_pt(V2P(window_table));
    window_table = 0;
    return 0;
}
