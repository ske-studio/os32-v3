#include "gfx2_int.h"
#include "paging.h"
#include "os32_kapi_shared.h"

#define T(x) do { if (!(x)) return __LINE__; } while (0)
static struct gfx2_adapter adapter;
static struct gfx2_surf surf;
static struct addrspace spaces[5];
static u32 pfns[3] = {0x100, 0x200, 0x300};
static u8 staging_pages[3 * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static u32 pages[16], attrs[16], allocated, frees, map_calls, unmap_calls;
static int map_fail, unmap_fail, alloc_fail, read_fail, write_fail, begin_fail;
static u32 sync_mode, begin_calls, begin_access, end_calls, reads, writes, order;
static int pending;
static struct addrspace *current;
void *kmemset(void *p, int c, unsigned int n)
{ u8 *b = p; while (n--) *b++ = c; return p; }
struct gfx2_adapter *gfx2_adapter_get(u32 i) { return i ? 0 : &adapter; }
int gfx2_caller_get(struct gfx2_caller *c)
{ c->as = current; c->owner = current->owner; c->app_id = 1; return 0; }
struct gfx2_surf *gfx2_surf_get(u32 sid, u32 owner, int *rc)
{
    if (sid != surf.sid || (owner != surf.owner && !(surf.flags & GFX2_SF_ALIAS))) {
        *rc = OS32_ERR_INVAL; return 0;
    }
    return &surf;
}
int gfx2_surf_state_set(struct gfx2_surf *s, u32 st) { s->state = st; return 0; }
int gfx2_sync_get(const struct gfx2_adapter *a, u32 head, GfxSyncCtx **ctx, u32 *mode)
{ (void)a; (void)head; *mode = sync_mode; *ctx = sync_mode ? (GfxSyncCtx *)&adapter : 0; return sync_mode ? 0 : OS32_ERR_NOTFOUND; }
int gfx2_cpu_begin(struct gfx2_surf *s, u32 access, u32 dl)
{ (void)s; (void)dl; begin_calls++; begin_access = access; return begin_fail; }
void gfx2_cpu_end(struct gfx2_surf *s, u32 access, const struct gfx2_rect *r)
{ (void)s; (void)access; (void)r; end_calls++; order = order * 10 + 2; }
int gfx2_fence_done(struct gfx2_adapter *a, u32 f) { (void)a; return !f || !pending; }
u32 paging_current_cr3(void) { return current->pd_phys; }
u32 paging_pte_flags(u32 va) { (void)va; return PAGE_RW | PTE_PCD; }
int gfx2_gart_pfns(const struct gfx2_surf *s, const u32 **out, u32 *n)
{ (void)s; *out = pfns; *n = 3; return 0; }
int paging_gfx2_map(struct addrspace *as, u32 va, u32 pa, u32 n, u32 flags)
{
    (void)as; map_calls++;
    if ((int)map_calls == map_fail) return -2;
    for (u32 i = 0; i < n; i++) {
        u32 k = (va - MEM_GFX2_MAP_BASE) / PAGE_SIZE + i;
        if (k >= 16 || pages[k]) return -1;
        pages[k] = pa + i * PAGE_SIZE; attrs[k] = flags;
    }
    return 0;
}
int paging_lease_unmap(struct addrspace *as, u32 va, u32 n)
{
    (void)as; unmap_calls++; order = order * 10 + 3;
    if (unmap_fail) return -1;
    for (u32 i = 0; i < n; i++) pages[(va - MEM_GFX2_MAP_BASE) / PAGE_SIZE + i] = 0;
    return 0;
}
u32 pgalloc_arena_end(void) { return 0x10000; }
int pgalloc_alloc_n_owner(u32 owner, int n, u32 lo, u32 hi, u32 flags, u32 *pfn)
{
    (void)lo; (void)hi; (void)flags;
    if (alloc_fail || owner != current->owner || n > 3) return 0;
    *pfn = (u32)(uptr)staging_pages / PAGE_SIZE; allocated += n; return 1;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    if (owner != current->owner || pfn != (u32)(uptr)staging_pages / PAGE_SIZE) return 0;
    allocated -= n; frees++; order = order * 10 + 4; return 1;
}
int gfxsync_read_rows(GfxSyncCtx *ctx, u32 x, u32 y, u32 w, u32 h, u8 *dst, u32 pitch)
{
    (void)ctx; (void)x; (void)y; (void)w; reads++;
    if (read_fail) return OS32_ERR_IO;
    for (u32 i = 0; i < h * pitch; i++) dst[i] = 0x5a;
    return 0;
}
int gfxsync_write_rows(GfxSyncCtx *ctx, u32 x, u32 y, u32 w, u32 h, const u8 *src, u32 pitch)
{
    (void)ctx; writes++; order = order * 10 + 1;
    if (x || y || w != surf.w || h != surf.h || src != surf.staging || pitch != surf.pitch) return OS32_ERR_IO;
    return write_fail ? OS32_ERR_IO : 0;
}
static void reset(void)
{
    for (u32 i = 0; i < 5; i++) {
        gfx2_vaspace_drop(&spaces[i]);
        spaces[i].generation = i + 1; spaces[i].pd_phys = (i + 1) * PAGE_SIZE;
        spaces[i].owner = i + 7;
    }
    current = &spaces[0];
    kmemset(&surf, 0, sizeof(surf)); kmemset(pages, 0, sizeof(pages));
    surf.sid = 1; surf.owner = current->owner; surf.state = GFX2_ST_IDLE;
    surf.w = 64; surf.h = 128; surf.pitch = 64; surf.bytes = 8192;
    surf.pool = GFX2_POOL_SYSMEM; surf.sys_pfn = 0x100; surf.sys_n = 2;
    map_fail = unmap_fail = alloc_fail = read_fail = write_fail = begin_fail = pending = 0;
    sync_mode = begin_calls = end_calls = reads = writes = order = allocated = frees = map_calls = unmap_calls = 0;
    kmemset(staging_pages, 0xcc, sizeof(staging_pages));
}
static void direct(void) { surf.flags = GFX2_SF_ALIAS; sync_mode = GFXSYNC_DIRECT; }
static int tests(void)
{
    u32 va, pitch, pa, attr; void *ptr;
    reset(); surf.pool = GFX2_POOL_GART; surf.bytes = 3 * PAGE_SIZE;
    map_fail = 2; va = 123;
    T(gfx2_map(&surf, current, &va) == OS32_ERR_NOSPC);
    T(map_calls == 2 && unmap_calls == 1 && !pages[0] && !surf.map_as && va == 123);
    T(pfns[0] == 0x100 && pfns[1] == 0x200 && pfns[2] == 0x300);
    map_fail = 0; T(!gfx2_map(&surf, current, &va)); T(va == MEM_GFX2_MAP_BASE);
    T(pages[0] == pfns[0] * PAGE_SIZE && pages[2] == pfns[2] * PAGE_SIZE);
    T(gfx2_cpu_visible(&surf)); current = &spaces[1]; T(!gfx2_cpu_visible(&surf));
    surf.kva = staging_pages; T(gfx2_cpu_visible(&surf)); surf.kva = 0;
    current = &spaces[0]; unmap_fail = 1; T(gfx2_unmap(&surf) == OS32_ERR_IO);
    T(surf.map_as == current && surf.map_npages == 3);
    unmap_fail = 0; T(!gfx2_unmap(&surf)); T(!surf.map_as && !surf.map_va && !surf.map_npages);
    T(!gfx2_map(&surf, current, &va) && va == MEM_GFX2_MAP_BASE);
    reset();
    for (u32 i = 0; i < 4; i++) { kmemset(pages, 0, sizeof(pages)); T(!gfx2_map(&surf, &spaces[i], &va)); gfx2_map_detach(&surf); }
    T(gfx2_map(&surf, &spaces[4], &va) == OS32_ERR_NOSPC);
    gfx2_vaspace_drop(&spaces[1]); kmemset(pages, 0, sizeof(pages)); T(!gfx2_map(&surf, &spaces[4], &va));
    reset(); surf.pool = GFX2_POOL_VRAM; adapter.vram_phys = 0x10000000; surf.gpu_off = PAGE_SIZE;
    T(!gfx2_surf_page(&surf, 1, &pa, &attr)); T(pa == 0x10002000 && attr == PTE_PCD);
    T(gfx2_surf_page(&surf, 2, &pa, &attr) == OS32_ERR_INVAL);
    reset(); surf.last_write = 42; pending = 1;
    T(gfx2_lock(1, GFX2_LOCK_DONOTWAIT, &ptr, &pitch) == OS32_ERR_BUSY && !begin_calls);
    T(!gfx2_lock(1, GFX2_LOCK_DISCARD, &ptr, &pitch));
    T(begin_calls == 1 && begin_access == LEDGER_PERM_RW);
    T(surf.state == GFX2_ST_LOCKED_RW && (u32)(uptr)ptr == MEM_GFX2_MAP_BASE);
    T(gfx2_lock(1, 0, &ptr, &pitch) == OS32_ERR_BUSY);
    T(!gfx2_unlock(1) && surf.state == GFX2_ST_IDLE && surf.map_as == current);
    T(gfx2_unlock(1) == OS32_ERR_INVAL);
    T(gfx2_lock(1, 8, &ptr, &pitch) == OS32_ERR_INVAL);
    T(!gfx2_lock(1, GFX2_LOCK_READONLY, &ptr, &pitch)); T(begin_access == LEDGER_PERM_RO);
    T(!gfx2_unlock(1));
    reset(); begin_fail = OS32_ERR_TIMEOUT;
    T(gfx2_lock(1, 0, &ptr, &pitch) == OS32_ERR_TIMEOUT && surf.state == GFX2_ST_IDLE && !surf.map_as);
    reset(); direct(); alloc_fail = 1;
    T(gfx2_lock(1, 0, &ptr, &pitch) == OS32_ERR_NOSPC && !surf.map_as && !surf.staging && surf.state == GFX2_ST_IDLE);
    reset(); direct(); T(!gfx2_lock(1, GFX2_LOCK_DISCARD, &ptr, &pitch));
    T((u32)(uptr)ptr == MEM_GFX2_MAP_BASE && ptr != surf.staging && pitch == surf.pitch);
    T(allocated == 2 && surf.staging_owner == current->owner && !reads);
    for (u32 i = 0; i < 2 * PAGE_SIZE; i++) T(staging_pages[i] == 0);
    T(!gfx2_surf_page(&surf, 0, &pa, &attr) && pa == (u32)(uptr)staging_pages && !attr);
    order = 0; T(!gfx2_unlock(1));
    T(order == 1234 && writes == 1 && !allocated && !surf.staging && !surf.map_as && surf.state == GFX2_ST_IDLE);
    T(!surf.staging_n && !surf.staging_owner && !surf.staging_pfn && !surf.staging_pitch);
    reset(); direct(); T(!gfx2_lock(1, 0, &ptr, &pitch)); T(reads == 1 && staging_pages[0] == 0x5a);
    unmap_fail = 1; T(gfx2_unlock(1) == OS32_ERR_IO);
    T(allocated == 2 && !frees && !surf.staging && !surf.map_as && surf.state == GFX2_ST_IDLE && !(surf.flags & GFX2_SF_KEEP));
    reset(); direct(); read_fail = 1;
    T(gfx2_lock(1, 0, &ptr, &pitch) == OS32_ERR_IO && !allocated && !surf.staging && !surf.map_as && surf.state == GFX2_ST_IDLE);
    reset(); direct(); map_fail = 1;
    T(gfx2_lock(1, 0, &ptr, &pitch) == OS32_ERR_NOSPC && !allocated && !surf.staging && surf.state == GFX2_ST_IDLE);
    reset(); direct(); T(!gfx2_lock(1, 0, &ptr, &pitch)); write_fail = 1; order = 0;
    T(gfx2_unlock(1) == OS32_ERR_IO && order == 1234 && !allocated && surf.state == GFX2_ST_IDLE);
    reset(); direct(); sync_mode = GFXSYNC_SHADOW;
    T(gfx2_lock(1, 0, &ptr, &pitch) == OS32_ERR_INVAL && !begin_calls);
    sync_mode = 0; T(gfx2_lock(1, 0, &ptr, &pitch) == OS32_ERR_INVAL);
    reset(); direct(); T(!gfx2_lock(1, 0, &ptr, &pitch)); surf.flags |= GFX2_SF_KEEP;
    gfx2_staging_drop(&surf); T(allocated == 2 && !surf.staging && !frees);
    return 0;
}
void _start(void)
{
    int rc = tests();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc ? rc % 254 + 1 : 0));
    __builtin_unreachable();
}
