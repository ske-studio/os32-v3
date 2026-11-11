#include "gfx2_int.h"
#include "paging.h"
#include "os32_kapi_shared.h"

#define T(x) do { if (!(x)) return __LINE__; } while (0)
static struct gfx2_adapter adapter;
static GfxBackend backend;
GfxBackend *g_backend = &backend;
static u32 begin_roles, end_roles;
static int fill_hook(int x, int y, int w, int h, u8 c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; return 7; }
static int blit_hook(int x, int y, int sx, int sy, int w, int h)
{ (void)x; (void)y; (void)sx; (void)sy; (void)w; (void)h; return 9; }
#define surf gfx2_surfs[0]
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
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
struct gfx2_adapter *gfx2_adapter_get(u32 i) { return i > 1 ? 0 : &adapter; }
int gfx2_caller_get(struct gfx2_caller *c)
{ c->as = current; c->owner = current->owner; c->app_id = 1; return 0; }
int gfx2_sync_get(const struct gfx2_adapter *a, u32 head, GfxSyncCtx **ctx, u32 *mode)
{ (void)a; (void)head; *mode = sync_mode; *ctx = sync_mode ? (GfxSyncCtx *)&adapter : 0; return sync_mode ? 0 : OS32_ERR_NOTFOUND; }
int gfx2_cpu_begin(struct gfx2_surf *s, u32 access, u32 dl)
{ (void)dl; begin_roles |= 1u << s->role; begin_calls++; begin_access = access; return begin_fail; }
void gfx2_cpu_end(struct gfx2_surf *s, u32 access, const struct gfx2_rect *r)
{ (void)access; (void)r; end_roles |= 1u << s->role; end_calls++; order = order * 10 + 2; }
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

struct gfx2_adapter *gfx2_active(void) { return &adapter; }
i32 gfx_screen_owner(void) { return 1; }
struct ledger_surface *ledger_surface_find(u32 backend, u32 role)
{
    (void)backend;
    return ledger_surfaces[role].npages ? &ledger_surfaces[role] : 0;
}
static int quiesce_rc;
int gfx2_surf_quiesce(struct gfx2_surf *s, u32 dl)
{ (void)s; return dl == 1000000u ? quiesce_rc : OS32_ERR_INVAL; }
static int tests(void)
{
    u32 sid, old, pitch; void *va;
    current = &spaces[0]; current->owner = 7; current->generation = 1; current->pd_phys = PAGE_SIZE;
    adapter.v1 = &backend; adapter.kind = GFX2_KIND_PEGC; adapter.heads = 1; adapter.flags = GFX2_AF_ACTIVE;
    ledger_surfaces[LEDGER_ROLE_DISPLAY] = (struct ledger_surface){
        .first = 0x100, .npages = 2, .width = 64, .height = 128, .pitch = 64,
        .format = GFX2_FMT_INDEX8, .backing = LEDGER_SB_VRAM};
    ledger_surfaces[LEDGER_ROLE_CLIENT] = ledger_surfaces[LEDGER_ROLE_DISPLAY];
    ledger_surfaces[LEDGER_ROLE_CLIENT].first = 0x200;
    ledger_surfaces[LEDGER_ROLE_CLIENT].backing = LEDGER_SB_RAM;
    sync_mode = GFXSYNC_DIRECT;
    v1_bridge_rebind();
    T(!gfx2_screen_surface(0, 0, GFX2_ROLE_DISPLAY, &sid));
    T(surf.owner == GFX2_OWNER_SCREEN && surf.flags == GFX2_SF_ALIAS);
    T(surf.kva == (u8 *)0x100000 && surf.pool == GFX2_POOL_VRAM);
    T(!v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_R) && begin_calls == 1);
    v1_bridge_end(GFX2_ROLE_DISPLAY, GFX2_CPU_W, 0); T(end_calls == 1);
    old = sid;
    T(!gfx2_lock(sid, 0, &va, &pitch)); T(allocated == 2 && surf.map_as && surf.staging);
    T(v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_W) == OS32_ERR_BUSY);
    T(v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_R) == OS32_ERR_BUSY);
    quiesce_rc = OS32_ERR_TIMEOUT; v1_bridge_rebind();
    T(surf.sid == old && surf.staging && surf.map_as && allocated == 2);
    quiesce_rc = 0; ledger_surfaces[LEDGER_ROLE_DISPLAY].width = 32;
    v1_bridge_rebind();
    T(surf.sid != old && surf.w == 32 && !surf.map_as && !surf.staging && !allocated);
    T(!writes && end_calls == 1 && frees == 1);
    T(gfx2_unlock(old) == OS32_ERR_STALE);
    T(!gfx2_lock(surf.sid, GFX2_LOCK_READONLY, &va, &pitch));
    T(!v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_R));
    T(v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_W) == OS32_ERR_BUSY);
    quiesce_rc = OS32_ERR_STALE; v1_bridge_rebind(); T(!surf.staging && !allocated);
    ledger_surfaces[LEDGER_ROLE_DISPLAY].lease_count = 1;
    T(v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_R) == OS32_ERR_BUSY);
    ledger_surfaces[LEDGER_ROLE_DISPLAY].lease_count = 0;
    begin_fail = OS32_ERR_TIMEOUT;
    T(v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_W) == OS32_ERR_TIMEOUT);
    begin_fail = 0; sync_mode = GFXSYNC_SHADOW; adapter.index = 1;
    v1_bridge_rebind(); T(surf.adapter == 1 && surf.kva == (u8 *)0x200000 && surf.pool == GFX2_POOL_SYSMEM);
    backend.fill_rect = fill_hook; backend.blit = blit_hook;
    begin_fail = OS32_ERR_TIMEOUT;
    begin_roles = end_roles = 0;
    T(gfx_hw_fill_rect(0, 0, 2, 2, 1) == 7);
    T(begin_roles == 6 && end_roles == 6);
    begin_roles = end_roles = 0;
    T(gfx_hw_blit(0, 0, 1, 1, 2, 2) == 9);
    T(begin_roles == 6 && end_roles == 6);
    backend.policy = GFX_POLICY_DIRECT;
    begin_roles = end_roles = 0;
    T(gfx_hw_fill_rect(0, 0, 2, 2, 1) == 7);
    T(begin_roles == 2 && end_roles == 2);
    begin_fail = 0;
    ledger_surfaces[LEDGER_ROLE_CLIENT].npages = 0;
    old = surf.sid; v1_bridge_rebind(); T(surf.sid != old && !surf.w && !surf.kva);
    T(v1_bridge_begin(0, GFX2_CPU_R) == OS32_ERR_INVAL);
    return 0;
}
void _start(void)
{
    int rc = tests();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc ? rc % 254 + 1 : 0));
    __builtin_unreachable();
}
