/* INDEX8 DIRECT commands. User ranges are checked before synchronous hooks. */
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "v2/gfx2_int.h"
#include "gfx_policy.h"
#include "gfx_hw_mono_cpu.h"
#include "../exec/exec.h"

/* Check both the strided extent and its last address without wrapping u32. */
static int transfer_range(u32 data, u32 pitch, u32 row, int h, int user, int write)
{
    u32 bytes;
    if (!data || pitch < row || (u32)(h - 1) > (~(u32)0 - row) / pitch)
        return 0;
    bytes = (u32)(h - 1) * pitch + row;
    if (bytes - 1 > ~(u32)0 - data) return 0;
    return !user || (write ? ring3_user_range_writable(data, bytes) :
                            ring3_user_range_ok(data, bytes));
}

static int submit_one(const GFX_Cmd *cmd, int user, int width, int height)
{
    /* Keep the validated command stable while backend hooks execute. */
    GFX_Cmd c = *cmd;
    int x = c.x, y = c.y, w = c.w, h = c.h;
    int sx = c.sx, sy = c.sy;
    if (c.op < GFX_CMD_FILL || c.op > GFX_CMD_MONO) return OS32_ERR_INVAL;
    if ((c.op == GFX_CMD_FILL && c.rop > GFX_ROP_XOR) ||
        (c.op != GFX_CMD_FILL && c.rop != GFX_ROP_COPY)) return OS32_ERR_INVAL;
    if ((c.op == GFX_CMD_PUT || c.op == GFX_CMD_MONO) &&
        (c.key < -1 || c.key > 255)) return OS32_ERR_INVAL;
    if (w <= 0 || h <= 0) return 0;
    if (c.op == GFX_CMD_PUT || c.op == GFX_CMD_MONO) {
        u32 row = c.op == GFX_CMD_PUT ? (u32)w : (u32)(w + 7) / 8;
        if (!transfer_range(c.data, c.pitch, row, h, user, 0))
            return OS32_ERR_INVAL;
    }
    if (c.op == GFX_CMD_COPY) {
        /* Cut source and destination by the same amount on each edge. */
        if (x < 0) { sx -= x; w += x; x = 0; }
        if (y < 0) { sy -= y; h += y; y = 0; }
        if (sx < 0) { x -= sx; w += sx; sx = 0; }
        if (sy < 0) { y -= sy; h += sy; sy = 0; }
        if (!gfx_clip_screen(&x, &y, &w, &h, width, height) ||
            sx >= width || sy >= height) return 0;
        if (w > width - sx) w = width - sx;
        if (h > height - sy) h = height - sy;
        return g_backend->blit(x, y, sx, sy, w, h);
    }
    if (!gfx_clip_screen(&x, &y, &w, &h, width, height)) return 0;
    switch (c.op) {
    case GFX_CMD_FILL:
        return g_backend->fill_rop(x, y, w, h, (u8)c.color, (u8)c.rop);
    case GFX_CMD_PUT: {
        u32 offset = (u32)(y - c.y) * c.pitch + (u32)(x - c.x);
        return g_backend->put(x, y, w, h, (const u8 *)(uptr)(c.data + offset),
                              c.pitch, c.key);
    }
    default:
        /* Preserve bit alignment: MONO receives the original image and clip. */
        if (c.key >= 0 && g_backend->mono)
            return g_backend->mono(c.x, c.y, c.w, c.h,
                                   (const u8 *)(uptr)c.data, c.pitch,
                                   (u8)c.color, c.key, x, y, w, h);
        return gfx_hw_mono_cpu(c.x, c.y, c.w, c.h,
                               (const u8 *)(uptr)c.data, c.pitch,
                               (u8)c.color, c.key, x, y, w, h);
    }
}

int gfx_hw_submit(const GFX_Cmd *cmds, u32 n, u32 gen, u32 *done)
{
    int user = ring3_call_from_user();
    GFX_ScreenInfo info = {0};
    int rc;
    if (!done || (user && !ring3_user_range_writable((u32)(uptr)done, sizeof(u32))))
        return OS32_ERR_INVAL;
    *done = 0;
    if (!g_backend || g_backend->policy != GFX_POLICY_DIRECT) return OS32_ERR_NOSYS;
    if (gen != gfx_policy_gen) return OS32_ERR_STALE;
    if (n > GFX_CMD_MAX || (n && (!cmds ||
        (u32)(uptr)cmds > ~(u32)0 - (n * sizeof(GFX_Cmd) - 1) ||
        (user && !ring3_user_range_ok((u32)(uptr)cmds, n * sizeof(GFX_Cmd))))))
        return OS32_ERR_INVAL;
    if (!n) return 0;
    rc = g_backend->query(&info);
    if (rc) return rc;
    (void)v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_R | GFX2_CPU_W);
    for (u32 i = 0; i < n; i++) {
        rc = submit_one(&cmds[i], user, info.width, info.height);
        if (rc) break;
        *done = i + 1;
    }
    v1_bridge_end(GFX2_ROLE_DISPLAY, GFX2_CPU_R | GFX2_CPU_W, 0);
    return rc;
}

int gfx_hw_get(int x, int y, int w, int h, u8 *dst, u32 pitch, u32 gen)
{
    int user = ring3_call_from_user();
    GFX_ScreenInfo info = {0};
    int ox = x, oy = y, rc;
    u32 offset;
    if (!g_backend || g_backend->policy != GFX_POLICY_DIRECT) return OS32_ERR_NOSYS;
    if (gen != gfx_policy_gen) return OS32_ERR_STALE;
    if (w <= 0 || h <= 0) return 0;
    if (!transfer_range((u32)(uptr)dst, pitch, (u32)w, h, user, 1))
        return OS32_ERR_INVAL;
    rc = g_backend->query(&info);
    if (rc) return rc;
    if (!gfx_clip_screen(&x, &y, &w, &h, info.width, info.height)) return 0;
    offset = ((u32)y - (u32)oy) * pitch + ((u32)x - (u32)ox);
    (void)v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_R);
    rc = g_backend->get(x, y, w, h, dst + offset, pitch);
    v1_bridge_end(GFX2_ROLE_DISPLAY, GFX2_CPU_R, 0);
    return rc;
}
