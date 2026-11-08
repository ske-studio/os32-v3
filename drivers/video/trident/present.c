/* RAM CLIENT transfer policy; direct VRAM CLIENT only synchronizes. */
#include "trident.h"
#include "pc98.h"
#include "gfx/gfx_internal.h"

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
        for (u32 i = 0; i < bytes;) {
            if ((d->present_method == TRI_PRESENT_DWORD_SYNC ||
                 d->present_method == TRI_PRESENT_DWORD_WAIT ||
                 d->present_method == TRI_PRESENT_DWORD_PLAIN) &&
                !((dst + i) & 3u) && bytes - i >= 4u) {
                const u8 *p = d->res->client + src + i;
                u32 value = (u32)p[0] | ((u32)p[1] << 8) |
                            ((u32)p[2] << 16) | ((u32)p[3] << 24);
                rc = trident_write32(d, TRI_SPACE_LFB, dst + i, value);
                if (rc) return rc;
                if (d->present_method == TRI_PRESENT_DWORD_SYNC) {
                    u8 status;
                    rc = trident_read8(d, TRI_SPACE_REGS, TRI_CR_COLOR + 6u, &status);
                    if (rc) return rc;
                } else if (d->present_method == TRI_PRESENT_DWORD_WAIT) {
                    rc = trident_port8(d, PC98_IO_WAIT_PORT, 0);
                    if (rc) return rc;
                }
                i += 4u;
                continue;
            }
            rc = trident_write8(d, TRI_SPACE_LFB, dst + i, d->res->client[src + i]);
            if (rc) return rc;
            ++i;
        }
    }
    return TRI_OK;
}
