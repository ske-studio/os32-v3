/* CPU transfer of the packed 8-bit client surface to the Trident LFB.
 * TRIDENT_S3_DESIGN §§3,7; X.Org trident_driver-1.4.0.c:3280-3282,
 * 3728-3736 selects the shadow framebuffer refresh path (MIT notice in
 * trident.h). The OS32 row-transfer interface is specified in trident.h. */
#include "trident.h"

/* Trial for PC-98 Ra266 hardware (2026-11-06): matching mode readbacks
 * still produced narrow, skewed bands with dword stores, while H's
 * mmiofill8 displayed correctly. Address decoding by access width remains
 * a hypothesis. Unlike X.Org trident_shadow.c:19-36 (memcpy) and :74-107
 * (CARD32 stores for rotation), force write8 here. Set to 1 to restore the
 * aligned span32 path; this choice does not change vram.c's dword probe. */
#define TRI_PRESENT_USE_SPAN32 0

int trident_present(struct trident_device *d, int x, int y, int w, int h)
{
    const struct trident_resources *r;
    u32 end;

    if (!d || !d->res || !d->io || !d->io->write8 ||
        (TRI_PRESENT_USE_SPAN32 && !d->io->write_span32))
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
        while (TRI_PRESENT_USE_SPAN32 && left && ((off & 3u) || ((uptr)src & 3u))) {
            rc = d->io->write8(d->io_opaque, TRI_SPACE_LFB, off, *src);
            if (rc) return rc;
            ++off; ++src; --left;
        }
        if (TRI_PRESENT_USE_SPAN32 && left >= 4u) {
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
