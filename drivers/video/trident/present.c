/* CPU transfer of the packed 8-bit client surface to the Trident LFB.
 * TRIDENT_S3_DESIGN §§3,7; X.Org trident_driver-1.4.0.c:3280-3282,
 * 3728-3736 selects the shadow framebuffer refresh path (MIT notice in
 * trident.h). The OS32 row-transfer interface is specified in trident.h. */
#include "trident.h"

int trident_present(struct trident_device *d, int x, int y, int w, int h)
{
    const struct trident_resources *r;
    u32 end;

    if (!d || !d->res || !d->io || !d->io->write8 || !d->io->write_span32)
        return TRI_E_INVAL;
    r = d->res;
    if (!r->client) return TRI_E_INVAL;
    /* Clip without adding positive extents to arbitrary signed origins. */
    if (w <= 0 || h <= 0 || x >= (int)TRI_WIDTH || y >= (int)TRI_HEIGHT)
        return TRI_OK;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w > (int)TRI_WIDTH - x) w = (int)TRI_WIDTH - x;
    if (h > (int)TRI_HEIGHT - y) h = (int)TRI_HEIGHT - y;
    if (w <= 0 || h <= 0) return TRI_OK;

    end = ((u32)y + (u32)h - 1u) * TRI_PITCH + (u32)x + (u32)w;
    if (end > r->client_bytes || end > r->mapped_bytes ||
        end > r->decode_bytes || end > d->verified_bytes)
        return TRI_E_ACCESS;

    for (u32 row = 0; row < (u32)h; ++row) {
        u32 off = ((u32)y + row) * TRI_PITCH + (u32)x;
        const u8 *src = r->client + off;
        u32 left = (u32)w;
        int rc;
        /* A span requires both source and destination to be aligned. */
        while (left && ((off & 3u) || ((uptr)src & 3u))) {
            rc = d->io->write8(d->io_opaque, TRI_SPACE_LFB, off, *src);
            if (rc) return rc;
            ++off; ++src; --left;
        }
        if (left >= 4u) {
            u32 count = left / 4u;
            rc = d->io->write_span32(d->io_opaque, off, (const u32 *)src, count);
            if (rc) return rc;
            off += count * 4u; src += count * 4u; left -= count * 4u;
        }
        while (left) {
            rc = d->io->write8(d->io_opaque, TRI_SPACE_LFB, off, *src);
            if (rc) return rc;
            ++off; ++src; --left;
        }
    }
    return TRI_OK;
}
