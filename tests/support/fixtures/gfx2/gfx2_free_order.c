/* Run C4 destruction through C6, retaining the allocator observations. */
#define _start gart_regression_start
#include "gfx2_gart.c"
#undef _start
static u32 unmap_at, release_at;
static struct gfx2_adapter adapter;
struct gfx2_adapter *gfx2_adapter_get(u32 id) { return &adapter; }
int gfx2_fence_done(struct gfx2_adapter *a, u32 f) { return 1; }
int gfx2_unmap(struct gfx2_surf *s) { unmap_at = ++seq; return 0; }
static int release(void *ctx, struct gfx2_surf *s) { release_at = ++seq; return 0; }
int gfx2_heap_free(struct gfx2_heap *h, u32 off) { return OS32_ERR_INVAL; }
int gfx2_sysmem_free(struct gfx2_surf *s) { return OS32_ERR_INVAL; }
void *kmemset(void *p, int v, u32 n)
{
    u8 *b = p;
    while (n--) *b++ = v;
    return p;
}
static int run(void)
{
    static const GfxBackendV2 backend = {.gart_bind = bind, .gart_unbind = unbind, .release = release};
    struct gfx2_surf *s = &gfx2_surfs[0];
    adapter.be = &backend;
    s->state = GFX2_ST_IDLE;
    s->pool = GFX2_POOL_GART;
    s->bytes = MEM_PAGE_SIZE;
    if (gfx2_gart_alloc(&adapter, s, 3)) return 1;
    if (gfx2_surf_destroy(s) || s->state != GFX2_ST_FREE ||
        unmap_at != 1 || release_at != 2 || unbind_at != 3 ||
        unpin_at != 4 || free_at != 5 || owners[2] || pins[2]) return 2;
    return 0;
}
void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(run()));
    __builtin_unreachable();
}
