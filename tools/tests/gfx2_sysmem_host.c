#include "gfx/v2/gfx2_int.h"
#include "paging.h"

static u8 pages[PAGE_SIZE * 3] __attribute__((aligned(PAGE_SIZE)));
static int fail_alloc, fail_free, allocated;
static u32 expected_owner = 7;

u32 pgalloc_arena_end(void) { return 0x12345; }
int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 flags, u32 *pfn)
{
    if (owner != expected_owner || n != 2 || first != MEM_PHYS_EXEC_FLOOR / PAGE_SIZE ||
        end != 0x12345 || flags != 0 || fail_alloc) return 0;
    allocated = 1;
    *pfn = (u32)(uptr)pages >> 12;
    return 1;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    if (owner != expected_owner || pfn != (u32)(uptr)pages >> 12 || n != 2 ||
        fail_free || !allocated) return 0;
    allocated = 0;
    return 1;
}
void *kmemset(void *dst, int value, u32 n)
{
    u8 *p = dst;
    for (u32 i = 0; i < n; ++i) p[i] = (u8)value;
    return dst;
}
static int test(void)
{
    struct gfx2_surf s = { .bytes = PAGE_SIZE + 1, .owner = 7 };
    for (u32 i = 0; i < sizeof(pages); ++i) pages[i] = 0xa5;
    fail_alloc = 1;
    if (gfx2_sysmem_alloc(&s, 7) != OS32_ERR_NOSPC || s.kva || s.sys_n) return 1;
    fail_alloc = 0;
    if (gfx2_sysmem_alloc(&s, 7) || s.sys_n != 2 || s.kva != pages ||
        s.sys_pfn != (u32)(uptr)pages >> 12) return 2;
    for (u32 i = 0; i < PAGE_SIZE * 2; ++i) if (pages[i]) return 3;
    if (pages[PAGE_SIZE * 2] != 0xa5) return 4;
    fail_free = 1;
    if (gfx2_sysmem_free(&s) != OS32_ERR_IO || s.kva != pages || s.sys_n != 2 || !allocated) return 5;
    fail_free = 0;
    if (gfx2_sysmem_free(&s) || s.kva || s.sys_n || s.sys_pfn || allocated) return 6;
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
