/* A0 placeholders; replaced by strong definitions in the owning stages. */
#include "sync_int.h"
__attribute__((weak)) int gfxsync_bind(const char *key, const GfxSyncTarget *t, GfxSyncCtx **out)
{
    (void)key;
    (void)t;
    (void)out;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfxsync_write_rows(GfxSyncCtx * a0, u32 x, u32 y, u32 w, u32 h, const u8 *src, u32 pitch)
{
    (void)a0;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)src;
    (void)pitch;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfxsync_read_rows(GfxSyncCtx * a0, u32 x, u32 y, u32 w, u32 h, u8 *dst, u32 pitch)
{
    (void)a0;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)dst;
    (void)pitch;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfxsync_flush(GfxSyncCtx * a0)
{
    (void)a0;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gfxsync_unbind(GfxSyncCtx * a0)
{
    (void)a0;
}
__attribute__((weak)) int gfxsync_mode(const GfxSyncCtx * a0)
{
    (void)a0;
    return 0;
}
__attribute__((weak)) int gfxsync_cfg_lookup(const char *key, struct gfxsync_cfg *out)
{
    (void)key;
    (void)out;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfxsync_cfg_check(const struct gfxsync_cfg * a0, const GfxSyncTarget * a1)
{
    (void)a0;
    (void)a1;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) u32 gfxsync_gap_loops(u32 loops_per_tick, u32 ns)
{
    (void)loops_per_tick;
    (void)ns;
    return 0;
}
__attribute__((weak)) int gfxsync_row_write(GfxSyncCtx * a0, u32 off, const u8 *src, u32 bytes)
{
    (void)a0;
    (void)off;
    (void)src;
    (void)bytes;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfxsync_row_read(GfxSyncCtx * a0, u32 off, u8 *dst, u32 bytes)
{
    (void)a0;
    (void)off;
    (void)dst;
    (void)bytes;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) GfxSyncCtx gfxsync_ctx_pool[4];
