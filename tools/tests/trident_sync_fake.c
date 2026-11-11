/* T1's synchronous boundary double, not an implementation of Y2/Y3. */
#include "drivers/video/trident/trident.h"
#include "gfx/sync/sync_int.h"
#include "gfx/v2/gfx2_int.h"
GfxSyncCtx gfxsync_ctx_pool[4];
static GfxSyncCtx *published;
int tri_test_bind_rc, tri_test_write_rc, tri_test_publish_rc, tri_test_order_bad;
u32 tri_test_binds, tri_test_unbinds, tri_test_writes, tri_test_unregisters;
int gfx2_sync_publish(u32 kind, u32 head, GfxSyncCtx *ctx)
{
    if (kind != GFX2_KIND_TRIDENT || head) return OS32_ERR_INVAL;
    if (ctx && tri_test_publish_rc) return tri_test_publish_rc;
    published = ctx;
    return 0;
}
void gfx2_adapter_unregister(GfxBackend *b) { (void)b; ++tri_test_unregisters; }
u32 gfxsync_gap_loops(u32 lpt, u32 ns)
{
    /* Tests use a fixed 1MHz calibration. Arithmetic is tested by Y2. */
    (void)lpt;
    return ns / 10;
}
int gfxsync_bind(const char *key, const GfxSyncTarget *t, GfxSyncCtx **out)
{
    if (tri_test_bind_rc) return tri_test_bind_rc;
    if (key[0] != 'p' || key[8] != ':') return OS32_ERR_INVAL;
    for (u32 i = 0; i < 4; ++i) if (!gfxsync_ctx_pool[i].used) {
        GfxSyncCtx *c = &gfxsync_ctx_pool[i];
        *c = (GfxSyncCtx){.t = *t, .used = 1,
            .cfg = {.mode = GFXSYNC_SHADOW, .unit = 1}};
        c->bind_cfg = c->cfg;
        *out = c; ++tri_test_binds; return 0;
    }
    return OS32_ERR_FULL;
}
void gfxsync_unbind(GfxSyncCtx *c)
{
    if (published == c) tri_test_order_bad = 1;
    c->used = 0; ++tri_test_unbinds;
}
int gfxsync_override(GfxSyncCtx *c, u32 method, u32 unit, u32 ns)
{
    if (!c || !c->used || method > 3 || ns > 100000) return OS32_ERR_INVAL;
    c->cfg.method = method; c->cfg.unit = unit; c->cfg.gap_ns = ns;
    return 0;
}
void gfxsync_override_clear(GfxSyncCtx *c)
{
    if (c) c->cfg = c->bind_cfg;
}
int gfxsync_write_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h,
                       const u8 *src, u32 pitch)
{
    ++tri_test_writes;
    if (tri_test_write_rc) return tri_test_write_rc;
    if (!c || !c->used || published != c) return OS32_ERR_INVAL;
    struct trident_device *d = c->t.opaque;
    u32 cpp = c->t.fmt == GFX2_FMT_INDEX8 ? 1 :
        c->t.fmt == GFX2_FMT_RGB565 ? 2 : 4;
    for (u32 row = 0; row < h; ++row) {
        u32 off = d->mode.display.off + (y + row) * c->t.pitch + x * cpp;
        for (u32 i = 0; i < w * cpp;) {
            u32 n = c->cfg.unit == 4 && !((off + i) & 3) && w * cpp - i >= 4 ? 4 : 1;
            const u8 *s = src + row * pitch + i;
            int rc = n == 1 ? trident_write8(d, TRI_SPACE_LFB, off + i, *s) :
                trident_write32(d, TRI_SPACE_LFB, off + i,
                    (u32)s[0] | ((u32)s[1] << 8) | ((u32)s[2] << 16) | ((u32)s[3] << 24));
            if (rc) return rc;
            if (n == 4 && c->cfg.method == GFXSYNC_M_SYNC_READ) rc = c->t.sync_read(d);
            if (n == 4 && c->cfg.method == GFXSYNC_M_WAIT_PORT) rc = c->t.wait_port(d);
            if (rc) return rc;
            i += n;
        }
    }
    return 0;
}
