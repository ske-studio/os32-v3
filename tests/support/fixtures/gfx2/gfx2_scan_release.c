#include "gfx2_int.h"

static struct gfx2_adapter adapter;
static unsigned released, retired;

struct gfx2_adapter *gfx2_adapter_get(u32 id) { return id ? NULL : &adapter; }
int gfx2_caller_get(struct gfx2_caller *c) { c->owner = 7; return 0; }
i32 gfx_screen_owner(void) { return 1; }
int gfx2_fence_done(struct gfx2_adapter *a, u32 f) { return !f; }
int gfx2_unmap(struct gfx2_surf *s) { return 0; }
int gfx2_heap_free(struct gfx2_heap *heap, u32 off) { return 0; }
int gfx2_gart_free(struct gfx2_adapter *a, struct gfx2_surf *s) { return OS32_ERR_INVAL; }
int gfx2_sysmem_free(struct gfx2_surf *s) { return OS32_ERR_INVAL; }
void gfx_policy_scan_changed(void) {}
void gfx2_quarantine_drain(struct gfx2_adapter *a) {}

void *kmemset(void *dst, int val, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)dst)[i] = val;
    return dst;
}

static int release(void *ctx, struct gfx2_surf *s)
{
    released = s->sid;
    return 0;
}

static int flip_done(void *ctx, u32 head, u32 *sid)
{
    *sid = retired;
    return retired ? 0 : OS32_ERR_BUSY;
}

static int mode_result;
static u32 scan_sid;

static int set_mode(void *ctx, u32 head, u32 mask, const GFX2_ModeInfo *mode,
                    const struct gfx2_surf *s)
{
    if (!mode_result) scan_sid = s->sid;
    return mode_result;
}

static int run(void)
{
    static const GfxBackendV2 be = {.release = release, .flip_done = flip_done, .mode_set = set_mode};
    struct gfx2_surf *old = &gfx2_surfs[0], *next = &gfx2_surfs[1];
    adapter.be = &be;
    adapter.heads = 1;
    *old = (struct gfx2_surf){.sid = 1024, .owner = 7, .state = GFX2_ST_IDLE,
                              .pool = GFX2_POOL_VRAM};
    next->sid = 1025;
    next->state = GFX2_ST_IDLE;
    gfx2_scan_ref_swap(&adapter, 0, old);
    adapter.head[0].pend_sid = next->sid;
    gfx2_scan_ref_get(next);
    if (gfx2_surface_free(old->sid) || old->state != GFX2_ST_CLOSING) return 1;
    gfx2_flip_poll(&adapter);
    gfx2_closing_sweep(&adapter);
    if (released || old->scan_ref != 1) return 1;
    retired = old->sid;
    gfx2_flip_poll(&adapter);
    gfx2_closing_sweep(&adapter);
    if (released != retired || old->state != GFX2_ST_FREE ||
        next->scan_ref != 1 || adapter.head[0].scan_sid != next->sid) return 2;
    static const GfxMode mode = {.width = 640, .height = 480};
    static GfxBackend v1 = {.selected_mode = &mode};
    struct gfx2_surf *alias = &gfx2_surfs[2];
    *alias = (struct gfx2_surf){.sid = 1026, .state = GFX2_ST_IDLE,
                                .flags = GFX2_SF_ALIAS, .role = GFX2_ROLE_DISPLAY};
    adapter.v1 = &v1;
    next->owner = 7;
    next->pool = GFX2_POOL_VRAM;
    released = 0;
    scan_sid = next->sid;
    mode_result = OS32_ERR_IO;
    if (gfx2_head_restore_v1(&adapter, 0) != OS32_ERR_IO) return 3;
    if (gfx2_surface_free(next->sid)) return 3;
    gfx2_closing_sweep(&adapter);
    if (released || adapter.head[0].scan_sid != scan_sid || next->scan_ref != 1 ||
        next->state != GFX2_ST_CLOSING) return 3;
    mode_result = 0;
    if (gfx2_head_restore_v1(&adapter, 0)) return 4;
    gfx2_closing_sweep(&adapter);
    if (released != 1025 || next->state != GFX2_ST_FREE || alias->scan_ref != 1 ||
        scan_sid != alias->sid || adapter.head[0].scan_sid != scan_sid) return 4;
    return 0;
}

void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(run()));
    __builtin_unreachable();
}
