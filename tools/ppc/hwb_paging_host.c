/* PowerMac HW_OP design §5 5c: master PT ownership and supervisor-only
 * window translations. The walker reads real host-backed software tables. */
#include "mm/paging.h"
#include "ppc_pd.h"
static u32 master[PDE_COUNT] __attribute__((aligned(4096)));
static u32 table[PTE_COUNT] __attribute__((aligned(4096)));
static u32 *page_directory = master, *page_tables[PDE_COUNT];
static u32 current, fail_alloc, flushes, frees, live_addrspaces;
static void quit(u32 code) __attribute__((noreturn));
static void quit(u32 code)
{
    __asm__ volatile("int $0x80" :: "a"(1), "b"(code));
    __builtin_unreachable();
}
#define EXPECT(x) do { if (!(x)) quit(__LINE__); } while (0)
u32 paging_kernel_pd_phys(void) { return (u32)master; }
u32 paging_current_cr3(void) { return current; }
static void arch_mmu_flush_tlb(void) { ++flushes; }
static void pgalloc_free_pt(u32 pa)
{
    EXPECT(pa == (u32)table && !page_tables[PDE_INDEX(PPC_HWB_WINDOW_VA)]);
    EXPECT(!master[PDE_INDEX(PPC_HWB_WINDOW_VA)] && flushes == 2);
    ++frees;
}
static int prepare_tables(u32 first, u32 count)
{
    EXPECT(first == PPC_HWB_WINDOW_VA / PAGE_SIZE && count == 2);
    if (fail_alloc) return -1;
    page_tables[first / PTE_COUNT] = table;
    master[first / PTE_COUNT] = (u32)table | PAGE_RW;
    return 0;
}
#include "platform_paging_window.h"
void _start(void)
{
    u32 va = PPC_HWB_WINDOW_VA, di = PDE_INDEX(va);
    current = (u32)master;
    fail_alloc = 1;
    EXPECT(paging_window_reserve() < 0 && !master[di] && !flushes);
    fail_alloc = 0;
    master[di] = 0x1100;
    EXPECT(paging_window_reserve() < 0 && master[di] == 0x1100);
    master[di] = 0;
    EXPECT(!paging_window_reserve() && flushes == 1);
    EXPECT(paging_window_reserve() < 0);
    struct ppc_translation t;
    for (u32 writable = 0; writable <= 1; writable++) {
        table[0] = 0xabc000 | PTE_PCD | PTE_PRESENT | (writable ? PTE_RW : 0);
        EXPECT(ppc_pd_lookup(current,va+8,0,0,&t));
        EXPECT(t.pa == 0xabc008 && t.attr == (writable ? 0x28u : 0x2bu));
        EXPECT(ppc_pd_lookup(current,va,1,0,&t) == (int)writable);
        EXPECT(!ppc_pd_lookup(current,va,0,1,&t));
        EXPECT(!ppc_pd_lookup(current,va,1,1,&t));
    }
    table[0] = 0;
    live_addrspaces = 1;
    EXPECT(paging_window_release() < 0);
    live_addrspaces = 0;
    current = 0;
    EXPECT(paging_window_release() < 0 && !frees);
    current = (u32)master;
    EXPECT(!paging_window_release() && frees == 1);
    EXPECT(paging_window_release() < 0 && frees == 1);
    quit(0);
}
