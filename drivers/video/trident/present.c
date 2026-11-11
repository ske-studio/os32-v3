/* RAM CLIENT transfer policy; direct VRAM CLIENT only synchronizes. */
#include "trident.h"
#include "pc98.h"
#include "gfx/gfx_internal.h"
#include "gfx/v2/gfx2_int.h"
#include "gfx/sync/sync_int.h"

int trident_present(struct trident_device *d, int x, int y, int w, int h)
{
    /* The HAL caller owns drawable/generation checks; clip inside present. */
    int rc = tri_sync(d);
    if (rc) return rc;
    if (!d->res->ram_fallback) return TRI_OK;
    const struct tri_surface *c = &d->mode.client, *v = &d->mode.display;
    if (!gfx_clip_screen(&x, &y, &w, &h, c->width, c->height)) return TRI_OK;
    u32 bytes = (u32)w * c->cpp;
    for (u32 row = (u32)y; row < (u32)y + (u32)h; ++row) {
        u32 src = row * c->pitch + (u32)x * c->cpp;
        u32 dst = v->off + row * v->pitch + (u32)x * c->cpp;
        if (src > c->bytes || bytes > c->bytes - src ||
            src > d->res->client_bytes || bytes > d->res->client_bytes - src ||
            dst < v->off || dst - v->off > v->bytes ||
            bytes > v->bytes - (dst - v->off) ||
            dst > d->res->mapped_bytes || bytes > d->res->mapped_bytes - dst ||
            dst > d->res->decode_bytes || bytes > d->res->decode_bytes - dst ||
            dst > d->declared_bytes || bytes > d->declared_bytes - dst)
            return TRI_E_CAP_TOO_SMALL;
    }
    rc = trident_sync_bind(d);
    if (rc) return rc;
    return gfxsync_write_rows(d->sync_ctx, (u32)x, (u32)y, (u32)w, (u32)h,
                              d->res->client + (u32)y * c->pitch + (u32)x * c->cpp,
                              c->pitch);
}

/* Hooks retain the production register/port seam; the common writer owns stores. */
static int sync_read(void *opaque)
{
    u8 status;
    return trident_read8(opaque, TRI_SPACE_REGS, TRI_CR_COLOR + 6u, &status);
}
static int wait_port(void *opaque)
{
    return trident_port8(opaque, PC98_IO_WAIT_PORT, 0);
}
static int read32(void *opaque, u32 off, u32 *v)
{
    struct trident_device *d = opaque;
    return trident_read32(d, TRI_SPACE_LFB, d->mode.display.off + off, v);
}
static int flush(void *opaque)
{
    u32 value;
    return read32(opaque, 0, &value);
}
void trident_sync_unbind(struct trident_device *d)
{
    if (!d->sync_ctx) return;
    gfx2_sync_publish(GFX2_KIND_TRIDENT, 0, NULL);
    gfxsync_unbind(d->sync_ctx);
    d->sync_ctx = NULL;
    d->sync_generation = 0;
}
int trident_sync_bind(struct trident_device *d)
{
    const struct tri_surface *v = &d->mode.display;
    if (d->sync_ctx && d->sync_generation == d->res->generation) return 0;
    trident_sync_unbind(d);
    u32 limit = d->res->mapped_bytes;
    if (limit > d->res->decode_bytes) limit = d->res->decode_bytes;
    if (limit > d->declared_bytes) limit = d->declared_bytes;
    if (!v->bytes || v->off > limit || v->bytes > limit - v->off)
        return TRI_E_CAP_TOO_SMALL;
    char key[] = "pci:1023:0000";
    static const char hex[] = "0123456789abcdef";
    for (u32 i = 0; i < 4; ++i)
        key[9 + i] = hex[(d->res->pci.device >> (12 - i * 4)) & 15];
    GfxSyncTarget t = {.va = d->res->lfb + v->off, .bytes = v->bytes,
        .pitch = v->pitch, .fmt = v->cpp == 1 ? GFX2_FMT_INDEX8 :
            v->cpp == 2 ? GFX2_FMT_RGB565 : GFX2_FMT_XRGB8888,
        .head = 0, .sync_read = sync_read, .wait_port = wait_port,
        .flush = flush, .read32 = read32, .opaque = d};
    int rc = gfxsync_bind(key, &t, &d->sync_ctx);
    if (rc) return rc;
    rc = gfx2_sync_publish(GFX2_KIND_TRIDENT, 0, d->sync_ctx);
    if (rc) { trident_sync_unbind(d); return rc; }
    d->sync_generation = d->res->generation;
    return 0;
}
int tri_present_override(struct trident_device *d, u32 method, u32 ns)
{
    static const u8 methods[] = {GFXSYNC_M_PLAIN, GFXSYNC_M_SYNC_READ,
        GFXSYNC_M_WAIT_PORT, GFXSYNC_M_GAP};
    if (method > TRI_PRESENT_DWORD_GAP) return TRI_E_INVAL;
    int rc = gfxsync_override(d->sync_ctx, methods[method],
                              method == TRI_PRESENT_BYTE ? 1 : 4, ns);
    if (rc) return rc;
    d->present_method = (enum tri_present_method)method;
    d->write_gap_ns = ns;
    d->write_gap_loops = gfxsync_gap_loops(d->write_gap_lpt, ns);
    return 0;
}
void tri_present_clear(struct trident_device *d)
{
    gfxsync_override_clear(d->sync_ctx);
}
