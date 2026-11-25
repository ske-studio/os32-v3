#include "sync_int.h"
#include "os32_gfx2.h"
#include "arch_cpu.h"

static u32 pixel_bytes(u8 fmt)
{
    switch (fmt) {
    case GFX2_FMT_INDEX8: case GFX2_FMT_A8: case GFX2_FMT_L8: return 1;
    case GFX2_FMT_RGB565: case GFX2_FMT_XRGB1555: case GFX2_FMT_ARGB1555:
    case GFX2_FMT_YUY2: case GFX2_FMT_UYVY: return 2;
    case GFX2_FMT_XRGB8888: case GFX2_FMT_ARGB8888: return 4;
    default: return 0;
    }
}

int gfxsync_row_write(GfxSyncCtx *c, u32 off, const u8 *src, u32 bytes)
{
    if (!c || !src || !c->t.va || off > c->t.bytes || bytes > c->t.bytes - off)
        return OS32_ERR_INVAL;
    for (u32 i = 0; i < bytes;) {
        u32 n = c->cfg.unit;
        if ((n != 2 && n != 4) || (((unsigned long)(c->t.va + off + i)) & (n - 1)) || bytes - i < n)
            n = 1;
        if (n == 4) {
            u32 v = (u32)src[i] | (u32)src[i+1] << 8 | (u32)src[i+2] << 16 | (u32)src[i+3] << 24;
            *(volatile u32 *)(c->t.va + off + i) = v;
        } else if (n == 2) {
            u16 v = (u16)((u32)src[i] | (u32)src[i+1] << 8);
            *(volatile u16 *)(c->t.va + off + i) = v;
        } else c->t.va[off + i] = src[i];
        i += n;
        if (c->cfg.method == GFXSYNC_M_GAP) {
            for (u32 j = 0; j < c->gap_loops; ++j) __asm__ volatile("" ::: "memory");
        } else if (c->cfg.method == GFXSYNC_M_SYNC_READ) {
            if (!c->t.sync_read || c->t.sync_read(c->t.opaque)) return OS32_ERR_IO;
        } else if (c->cfg.method == GFXSYNC_M_WAIT_PORT) {
            if (!c->t.wait_port || c->t.wait_port(c->t.opaque)) return OS32_ERR_IO;
        }
    }
    return 0;
}

int gfxsync_row_read(GfxSyncCtx *c, u32 off, u8 *dst, u32 bytes)
{
    if (!c || !dst || off > c->t.bytes || bytes > c->t.bytes - off) return OS32_ERR_INVAL;
    if (c->cfg.read != GFXSYNC_R_DIRECT || !c->t.read32) return OS32_ERR_INVAL;
    for (u32 i = 0; i < bytes;) {
        u32 pos = off + i, base = pos & ~3u, v;
        if (base > c->t.bytes || c->t.bytes - base < 4) return OS32_ERR_INVAL;
        if (c->t.read32(c->t.opaque, base, &v)) return OS32_ERR_IO;
        do { dst[i++] = (u8)(v >> ((pos & 3u) * 8)); ++pos; }
        while (i < bytes && (pos & 3u));
    }
    return 0;
}

static int rows_check(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, u32 pitch, const void *p, u32 *start, u32 *len)
{
    if (!c || !p || !w || !h || !c->t.pitch) return OS32_ERR_INVAL;
    u32 cpp = pixel_bytes(c->t.fmt);
    if (!cpp || x > (u32)-1 - w || x > (u32)-1 / cpp || w > (u32)-1 / cpp) return OS32_ERR_INVAL;
    u32 xb = x * cpp, n = w * cpp;
    if (xb > c->t.pitch || n > c->t.pitch - xb || pitch < n ||
        y > (u32)-1 / c->t.pitch || h - 1 > (u32)-1 - y) return OS32_ERR_INVAL;
    u32 row = y * c->t.pitch;
    if (row > c->t.bytes || xb > c->t.bytes - row) return OS32_ERR_INVAL;
    row += xb;
    if (n > c->t.bytes - row || h - 1 > (c->t.bytes - row - n) / c->t.pitch ||
        (h > 1 && h - 1 > ((u32)-1 - n) / pitch)) return OS32_ERR_INVAL;
    *start = row; *len = n;
    return 0;
}

int gfxsync_write_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, const u8 *src, u32 pitch)
{
    u32 off, n;
    int rc = rows_check(c, x, y, w, h, pitch, src, &off, &n);
    if (rc) return rc;
    for (u32 i = 0; i < h; ++i) {
        rc = gfxsync_row_write(c, off + i * c->t.pitch, src + i * pitch, n);
        if (rc) return rc;
    }
    return 0;
}
int gfxsync_read_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, u8 *dst, u32 pitch)
{
    u32 off, n;
    int rc = rows_check(c, x, y, w, h, pitch, dst, &off, &n);
    if (rc) return rc;
    for (u32 i = 0; i < h; ++i) {
        rc = gfxsync_row_read(c, off + i * c->t.pitch, dst + i * pitch, n);
        if (rc) return rc;
    }
    return 0;
}
int gfxsync_flush(GfxSyncCtx *c)
{
    if (!c) return OS32_ERR_INVAL;
    if (c->cfg.flush == GFXSYNC_F_READBACK)
        return c->t.flush && !c->t.flush(c->t.opaque) ? 0 : OS32_ERR_IO;
    if (c->cfg.flush == GFXSYNC_F_SERIALIZE) arch_cpu_serialize();
    return 0;
}
