/* I3 guest acceptance. Run from CUI; GeForce exercises VRAM scanout. */
#include "os32api.h"
#include <string.h>

#define ATTR 7
#define WAIT_MS 1000u
static int exercise(KernelAPI *api, u32 a, const GFX2_AdapterInfo *info);
static int fail(KernelAPI *api, const char *stage, int rc);

/* OS32X entry must precede helpers. */
int main(int argc, char **argv, KernelAPI *api)
{
    int rc = 1;
    (void)argc; (void)argv;
    api->gfx_init();
    int n = api->gfx2_adapter_count();
    api->kprintf(ATTR, "gfx2_i3 adapters=%d\n", n);
    for (int a = 0; a < n; a++) {
        GFX2_AdapterInfo info;
        if (api->gfx2_adapter_info(a, &info)) { fail(api, "adapter", a); break; }
        api->kprintf(ATTR, "gfx2_i3 adapter=%d %s heads=%u connectors=%u\n",
                     a, info.name, info.heads, info.connectors);
        if (info.flags & GFX2_AF_ACTIVE) { rc = exercise(api, a, &info); break; }
    }
    api->gfx_shutdown();
    api->kprintf(ATTR, "gfx2_i3 %s rc=%d\n", rc ? "FAIL" : "PASS", rc);
    return rc ? 1 : 0;
}

static int fail(KernelAPI *api, const char *stage, int rc)
{
    api->kprintf(ATTR, "gfx2_i3 stage=%s rc=%d\n", stage, rc);
    return 1;
}

static int exercise(KernelAPI *api, u32 a, const GFX2_AdapterInfo *info)
{
    u32 front = 0, next = 0, token = 0, gen, display, done, fence, seen;
    int rc, result = 1, switched = 0;
    GFX2_Caps caps;
    GFX2_HeadInfo head;
    GFX2_ModeInfo mode;
    GFX2_SurfaceDesc desc;
    OS32_SurfaceQueryResult query;
    OS32_LeaseView lease;
    MemStat before = {0}, after;
#define CALL(stage, call) do { rc=(call); if (rc) { fail(api, stage, rc); goto out; } } while (0)
#define REQUIRE(stage, cond) do { if (!(cond)) { fail(api, stage, 1); goto out; } } while (0)
    REQUIRE("mem-before", api->mem_stat(0, &before, sizeof(before)) == (int)sizeof(before));
    CALL("caps", api->gfx2_caps(a, &caps, sizeof(caps)));
    CALL("head", api->gfx2_head_info(a, 0, &head));
    api->kprintf(ATTR, "gfx2_i3 sync=%u profile=%s cpu=%u\n", caps.sync_mode, caps.sync_profile, caps.cpu_direct);
    if (info->backend_kind == OS32_SURFACE_PEGC || info->backend_kind == OS32_SURFACE_TRIDENT)
        REQUIRE("sync-bound", caps.cpu_direct && caps.sync_mode == 1 && caps.sync_profile[0]);
    u32 conn_mask = 0;
    for (u32 c=0; c<info->connectors; c++) {
        GFX2_ConnectorInfo ci;
        CALL("connector", api->gfx2_connector_info(a, c, &ci));
        if (!conn_mask && (ci.possible_heads & 1) && ci.status != GFX2_CONN_OFF) conn_mask = 1u << c;
    }
    int count = api->gfx2_mode_count(a, 0, GFX2_FMT_INDEX8);
    REQUIRE("mode-count", count > 0 && conn_mask);
    CALL("mode", api->gfx2_mode_get(a, 0, GFX2_FMT_INDEX8, 0, &mode));
    REQUIRE("mode-end", api->gfx2_mode_get(a, 0, GFX2_FMT_INDEX8, count, &mode) == OS32_ERR_INVAL);
    CALL("fullscreen-query", api->surface_query(OS32_SURFACE_DISPLAY, &query));
    REQUIRE("one-display", query.count == 1);
    CALL("fullscreen-lease", api->surface_lease(OS32_SURFACE_DISPLAY, &query.desc[0].ref, OS32_SURFACE_RW, &lease));
    token = lease.token;
    CALL("display", api->gfx2_screen_surface(a, 0, GFX2_ROLE_DISPLAY, &display));
    CALL("display-desc", api->gfx2_surface_desc(display, &desc));
    CALL("gen", api->gfx2_adapter_gen(a, &gen));
    GFX2_Op fill = {.op=GFX2_OP_FILL, .dst=display, .dw=32, .dh=4,
        .rop=GFX2_ROP_PATCOPY, .fg={.v=5}};
    CALL("sync-fill", api->gfx2_submit(a, &fill, 1, gen, &done, &fence));
    REQUIRE("sync-done", done == 1);
    CALL("sync-present", api->gfx2_present(a, 0, 0, 0, 32, 4));
    u8 pixels[128];
    GFX2_Op read = {.op=GFX2_OP_READBACK, .src=display, .sw=32, .sh=4,
        .pitch=32, .data=(u32)pixels};
    CALL("sync-read", api->gfx2_submit(a, &read, 1, gen, &done, &fence));
    for (u32 i=0; i<sizeof(pixels); i++) REQUIRE("sync-pixel", pixels[i] == 5);
    u8 pool = info->backend_kind == OS32_SURFACE_GEFORCE ? GFX2_POOL_VRAM : GFX2_POOL_SYSMEM;
    CALL("front", api->gfx2_surface_create(a, mode.w, mode.h, mode.fmt, pool,
                                           GFX2_USE_SCANOUT | GFX2_USE_RENDER2D, &front));
    CALL("next", api->gfx2_surface_create(a, mode.w, mode.h, mode.fmt, pool,
                                          GFX2_USE_SCANOUT | GFX2_USE_RENDER2D, &next));
    fill.dst = front; fill.dw = mode.w; fill.dh = mode.h; fill.fg.v = 1;
    CALL("front-fill", api->gfx2_submit(a, &fill, 1, gen, &done, &fence));
    fill.dst = next; fill.fg.v = 2;
    CALL("next-fill", api->gfx2_submit(a, &fill, 1, gen, &done, &fence));
    rc = api->gfx2_mode_set(a, 0, conn_mask, mode.id, front);
    if (info->backend_kind == OS32_SURFACE_GEFORCE) {
        if (rc) { fail(api, "mode-set", rc); goto out; }
        switched = 1;
        CALL("scanout-head", api->gfx2_head_info(a, 0, &head));
        REQUIRE("front-sid", head.scanout_sid == front);
        CALL("close-front", api->gfx2_surface_free(front)); front = 0;
        CALL("flip", api->gfx2_flip(a, 0, next, GFX2_FLIP_VBLANK, WAIT_MS));
        CALL("vblank", api->gfx2_vblank_wait(a, 0, 0, WAIT_MS, &seen));
        u32 completed, failed;
        CALL("poll", api->gfx2_fence_query(a, &completed, &failed));
        CALL("flipped-head", api->gfx2_head_info(a, 0, &head));
        REQUIRE("next-sid", head.scanout_sid == next);
    } else {
        REQUIRE("mode-nosys", rc == OS32_ERR_NOSYS);
        REQUIRE("flip-nosys", api->gfx2_flip(a, 0, next, 0, WAIT_MS) == OS32_ERR_NOSYS);
        api->kprintf(ATTR, "gfx2_i3 mode/flip NOSYS (backend contract); full path requires GeForce\n");
    }
    CALL("fullscreen-leave", api->surface_unlease(token)); token = 0;
    if (switched) {
        CALL("restored-head", api->gfx2_head_info(a, 0, &head));
        REQUIRE("restored-display", head.scanout_sid == display);
    }
    result = 0;
out:
    if (token && (rc=api->surface_unlease(token))) result = fail(api, "cleanup-lease", rc);
    if (front && (rc=api->gfx2_surface_free(front))) result = fail(api, "cleanup-front", rc);
    if (next && (rc=api->gfx2_surface_free(next))) result = fail(api, "cleanup-next", rc);
    if (api->mem_stat(0, &after, sizeof(after)) != (int)sizeof(after) || after.audit_fail != before.audit_fail)
        result = fail(api, "mem-audit", 1);
    return result;
#undef CALL
#undef REQUIRE
}
