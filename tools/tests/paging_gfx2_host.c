typedef unsigned int u32;
#define PAGE_SIZE 4096u
#define PAGE_SHIFT 12
#define PDE_SHIFT 22
#define PTE_COUNT 1024
#define MEM_LEASE_BASE 0xf0000000u
#define MEM_GFX2_MAP_BASE 0xf8000000u
#define MEM_GFX2_MAP_END 0xfe000000u
#define MEM_LEASE_MAX_PDES 56
#define LEASE_PDE 960
#define PTE_PRESENT 1u
#define PTE_RW 2u
#define PAGE_RW 3u
#define PTE_USER 4u
#define PTE_PWT 8u
#define PTE_PCD 16u
#define PTE_PS 128u
struct addrspace { u32 pd_phys, owner, lease_pt_phys[MEM_LEASE_MAX_PDES]; };
static u32 pages[8][PTE_COUNT], used[8], allocs, frees, fail_at, cr3, reloads;
static int valid = 1;
#define P2V(p) (pages[(p) / PAGE_SIZE])
static int lease_context(struct addrspace *as) { return as && valid; }
static u32 pgalloc_alloc_phys(u32 owner, u32 n) {
    (void)owner; (void)n; allocs++;
    if (allocs == fail_at) return 0;
    for (u32 i = 2; i < 8; i++) if (!used[i]) { used[i] = 1; return i * PAGE_SIZE; }
    return 0;
}
static int pgalloc_free_n_owner(u32 owner, u32 pfn, u32 n) {
    (void)owner; (void)n; used[pfn] = 0; frees++; return 1;
}
static unsigned int irq_save(void) { return 0; }
static void irq_restore(unsigned int x) { (void)x; }
static u32 paging_current_cr3(void) { return cr3; }
static void paging_load_cr3(u32 x) { (void)x; reloads++; }
#include "paging_gfx2.inc"
static void die(int code) { __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); __builtin_unreachable(); }
#define CHECK(x) do { if (!(x)) die(1); } while (0)
void _start(void) {
    struct addrspace as = { .pd_phys = PAGE_SIZE, .owner = 7 };
    u32 va = MEM_GFX2_MAP_BASE, flags = PTE_PRESENT | PTE_USER | PTE_RW;
    u32 k = (va - MEM_LEASE_BASE) >> PDE_SHIFT;
    CHECK(paging_gfx2_map(&as, va - PAGE_SIZE, 0, 1, flags) == -1);
    CHECK(paging_gfx2_map(&as, va + 1, 0, 1, flags) == -1);
    CHECK(paging_gfx2_map(&as, va, 1, 1, flags) == -1);
    CHECK(paging_gfx2_map(&as, va, 0, 0, flags) == -1);
    CHECK(paging_gfx2_map(&as, MEM_GFX2_MAP_END - PAGE_SIZE, 0, 2, flags) == -1);
    CHECK(paging_gfx2_map(&as, va, 0xfffff000u, 2, flags) == -1);
    CHECK(paging_gfx2_map(&as, va, 0, 1, flags | PTE_PS) == -1);
    CHECK(paging_gfx2_map(&as, va, 0, 1, PTE_PRESENT) == -1);
    valid = 0; CHECK(paging_gfx2_map(&as, va, 0, 1, flags) == -1); valid = 1;
    fail_at = 2;
    CHECK(paging_gfx2_map(&as, va + (PTE_COUNT - 1) * PAGE_SIZE, 0, 2, flags) == -2);
    CHECK(!as.lease_pt_phys[k] && !as.lease_pt_phys[k+1]);
    CHECK(!pages[1][LEASE_PDE+k] && !pages[1][LEASE_PDE+k+1] && frees == 1 && !used[2]);
    fail_at = 0; cr3 = as.pd_phys;
    CHECK(paging_gfx2_map(&as, va, 0xfffff000u, 1, flags) == 0);
    CHECK(reloads == 1 && pages[as.lease_pt_phys[k]/PAGE_SIZE][0] == (0xfffff000u | flags));
    CHECK(paging_gfx2_map(&as, va, 0, 2, flags) == -1);
    CHECK(pages[as.lease_pt_phys[k]/PAGE_SIZE][0] == (0xfffff000u | flags));
    CHECK(!pages[as.lease_pt_phys[k]/PAGE_SIZE][1] && reloads == 1);
    cr3 = 0; CHECK(paging_gfx2_map(&as, va + PAGE_SIZE, 0, 1, flags | PTE_PCD) == 0);
    CHECK(reloads == 1 && pages[as.lease_pt_phys[k]/PAGE_SIZE][1] == (flags | PTE_PCD));
    die(0);
}
