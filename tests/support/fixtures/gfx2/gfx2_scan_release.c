#include "gfx2_int.h"

static struct gfx2_adapter adapter;
static unsigned released, retired;

struct gfx2_adapter *gfx2_adapter_get(u32 id) { return id ? NULL : &adapter; }
int gfx2_caller_get(struct gfx2_caller *c) { c->app_id = 1; c->owner = 7; return 0; }
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

const GfxBackendV2 gfx2_v1_shim;
void gfx2_adapter_recovered(struct gfx2_adapter *a) {}
int v1_bridge_rebind(void) { return 0; }
int gfx_fullscreen_owner(void) { return 0; }

static int enum_conn(void *ctx, u32 conn, GFX2_ConnectorInfo *out)
{
    out->possible_heads = 1;
    return 0;
}

static int enum_mode(void *ctx, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *out)
{
    if (i || fmt != GFX2_FMT_INDEX8) return OS32_ERR_INVAL;
    *out = (GFX2_ModeInfo){.id = 1, .fmt = fmt, .w = 8, .h = 8};
    return 0;
}

static unsigned flips;
static int flip_result;
int appslot_gui_present_ok(void) { return 1; }
int gfx2_surf_quiesce(struct gfx2_surf *s, u32 dl) { return 0; }
static int flip(void *ctx, u32 head, const struct gfx2_surf *s, u32 flags, u32 dl)
{
    ++flips;
    return flip_result;
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
    static const GfxBackendV2 be = {.enum_conn = enum_conn, .enum_mode = enum_mode, .flip = flip, .release = release, .flip_done = flip_done, .mode_set = set_mode};
    struct gfx2_surf *old = &gfx2_surfs[0], *next = &gfx2_surfs[1];
    adapter.be = &be;
    adapter.heads = 1;
    adapter.flags = GFX2_AF_ACTIVE;
    *old = (struct gfx2_surf){.sid = 1024, .owner = 7, .state = GFX2_ST_IDLE,
                              .pool = GFX2_POOL_VRAM};
    next->sid = 1025;
    next->state = GFX2_ST_IDLE;
    adapter.caps.connectors = 1;
    old->fmt = GFX2_FMT_INDEX8;
    old->w = old->h = 8;
    old->usage = GFX2_USE_SCANOUT;
    next->owner = 7;
    next->usage = GFX2_USE_SCANOUT;
    next->pool = GFX2_POOL_VRAM;
    if (gfx2_flip(0, 0, next->sid, 0, 10) != OS32_ERR_INVAL || flips ||
        adapter.head[0].scan_sid || adapter.head[0].pend_sid ||
        old->scan_ref || next->scan_ref) return 7;
    if (gfx2_mode_set(0, 0, 1, 1, old->sid) || scan_sid != old->sid) return 8;
    flip_result = OS32_ERR_TIMEOUT;
    if (gfx2_flip(0, 0, next->sid, 0, 10) != flip_result ||
        adapter.head[0].pend_sid || next->scan_ref || old->scan_ref != 1) return 5;
    flip_result = OS32_ERR_AGAIN;
    if (gfx2_flip(0, 0, next->sid, 0, 10) != flip_result ||
        adapter.head[0].pend_sid != next->sid || next->scan_ref != 1 ||
        adapter.head[0].scan_sid != old->sid || old->scan_ref != 1) return 6;
    if (gfx2_surface_free(old->sid) || old->state != GFX2_ST_CLOSING) return 1;
    if (gfx2_surface_free(next->sid) || next->state != GFX2_ST_CLOSING) return 6;
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

    gfx2_closing_sweep(&adapter);
    if (released || adapter.head[0].scan_sid != scan_sid || next->scan_ref != 1 ||
        next->state != GFX2_ST_CLOSING) return 3;
    mode_result = 0;
    if (gfx2_head_restore_v1(&adapter, 0)) return 4;
    gfx2_closing_sweep(&adapter);
    if (released != 1025 || next->state != GFX2_ST_FREE || alias->scan_ref != 1 ||
        scan_sid != alias->sid || adapter.head[0].scan_sid != scan_sid) return 4;
    struct gfx2_surf *pending = &gfx2_surfs[3];
    *pending = (struct gfx2_surf){.sid = 1027, .owner = 7, .state = GFX2_ST_IDLE,
                                  .pool = GFX2_POOL_VRAM, .usage = GFX2_USE_SCANOUT};
    *old = (struct gfx2_surf){.sid = 2048, .owner = 7, .state = GFX2_ST_IDLE,
                              .pool = GFX2_POOL_VRAM, .usage = GFX2_USE_SCANOUT,
                              .fmt = GFX2_FMT_INDEX8, .w = 8, .h = 8};
    retired = 0;
    if (gfx2_flip(0, 0, pending->sid, 0, 10) != OS32_ERR_AGAIN ||
        gfx2_surface_free(pending->sid)) return 9;
    mode_result = OS32_ERR_IO;
    if (gfx2_mode_set(0, 0, 1, 1, old->sid) != OS32_ERR_IO ||
        pending->scan_ref != 1 || alias->scan_ref != 1) return 9;
    mode_result = 0;
    if (gfx2_mode_set(0, 0, 1, 1, old->sid)) return 9;
    gfx2_closing_sweep(&adapter);
    if (pending->state != GFX2_ST_FREE || released != 1027 ||
        old->scan_ref != 1 || alias->scan_ref) return 9;
    flip_result = 0;
    retired = old->sid;
    if (gfx2_flip(0, 0, old->sid, 0, 10)) return 9;
    return 0;
}

void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(run()));
    __builtin_unreachable();
}
