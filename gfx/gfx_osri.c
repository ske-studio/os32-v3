#include "gfx_osri.h"
#include "gfx_hal.h"
#include "vfs.h"
#include "../lib/le_internal.h"
#include "../lib/crc32_core.inc"

#define OSRI_READ_CHUNK 4096u
#define OSRI_CRC_OFF 48u
#ifdef SPLASH_RASTER_OBS
#define OSRI_ALLOW_SAME(version) ((version) == OSRI_VERSION_2)
#else
#define OSRI_ALLOW_SAME(version) 0
#endif

static int read_part(int fd, u8 *p, u32 n, u32 *crc)
{
    while (n) {
        u32 size = n > OSRI_READ_CHUNK ? OSRI_READ_CHUNK : n;
        int got = vfs_read_fd(fd, p, size);
        if (got <= 0 || (u32)got > size) return -1;
        *crc = crc32_core_update(*crc, p, (u32)got);
        p += got;
        n -= (u32)got;
    }
    return 0;
}

int osri_load(const char *path, struct gfx_kernel_fb *dst, struct OsriImage *image)
{
    u8 h[OSRI_HEADER_BYTES], pal[OSRI_COLORS * 2], e[OSRI_EVENT_BYTES], extra;
    u16 current[OSRI_COLORS], version;
    u32 crc = CRC32_INIT, expected, count, band, plane, i, line_count = 0;
#ifdef SPLASH_RASTER_OBS
    u16 seen = 0;
#endif
    int fd, result = -1;
    if (!path || !dst || !image || dst->width != GFX_WIDTH ||
        dst->height != GFX_HEIGHT || dst->pitch != GFX_BPL ||
        dst->format != GFX_BB_PLANAR4) return -1;
    plane = dst->pitch * dst->height;
    if (plane != GFX_PLANE_SZ || !dst->planes[0]) return -1;
    for (i = 1; i < GVRAM_PLANES; i++)
        if (dst->planes[i] != dst->planes[0] + i * plane) return -1;
    fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return -1;
    /* Header CRC field is zeroed before including it in the stream. */
    if (read_part(fd, h, sizeof(h), &crc)) goto done;
    expected = le_read32(h + OSRI_CRC_OFF);
    for (i = OSRI_CRC_OFF; i < OSRI_CRC_OFF + 4; i++) h[i] = 0;
    crc = crc32_core_update(CRC32_INIT, h, sizeof(h));
    count = le_read32(h + 36); band = le_read16(h + 44);
    version = le_read16(h + 4);
    /* Bound the version's count before computing the exact file length. */
    if ((version != OSRI_VERSION_1 && version != OSRI_VERSION_2) ||
#ifdef SPLASH_RASTER_OBS
        count > (version == OSRI_VERSION_1 ? OSRI_V1_MAX_EVENTS : OBS_V2_MAX_EVENTS)) goto done;
#else
        count > (version == OSRI_VERSION_1 ? OSRI_V1_MAX_EVENTS : OSRI_V2_MAX_EVENTS)) goto done;
#endif
    if (h[0] != 'O' || h[1] != 'S' || h[2] != 'R' || h[3] != 'I' ||
        le_read16(h + 6) != OSRI_HEADER_BYTES ||
        le_read16(h + 8) != GFX_WIDTH || le_read16(h + 10) != GFX_HEIGHT ||
        h[12] != 4 || h[13] != 1 || le_read16(h + 14) ||
        le_read32(h + 16) != OSRI_EVENTS_OFF + count * OSRI_EVENT_BYTES ||
        le_read32(h + 20) != OSRI_PALETTE_OFF || le_read32(h + 24) != OSRI_PIXELS_OFF ||
        le_read32(h + 28) != OSRI_PIXELS_BYTES || le_read32(h + 32) != OSRI_EVENTS_OFF ||
        le_read16(h + 40) != OSRI_EVENT_BYTES || le_read16(h + 42) ||
        (band != 1 && band != 2 && band != 4) ||
        (version == OSRI_VERSION_1 && !count && band != 4) ||
        le_read16(h + 46) || le_read32(h + 52) || le_read32(h + 56) || le_read32(h + 60)) goto done;
    if (read_part(fd, pal, sizeof(pal), &crc)) goto done;
    for (i = 0; i < OSRI_COLORS; i++) {
        current[i] = image->pal[i] = le_read16(pal + 2 * i);
        if (current[i] > 0xfff) goto done;
    }
    for (i = 0; i < GVRAM_PLANES; i++)
        if (read_part(fd, dst->planes[i], plane, &crc)) goto done;
    image->count = (u16)count; image->band = (u16)band;
    image->version = version;
    for (i = 0; i < count; i++) {
        struct OsriEvent *ev = &image->event[i];
        if (read_part(fd, e, sizeof(e), &crc)) goto done;
        ev->line = le_read16(e); ev->index = e[2]; ev->reserved0 = e[3];
        ev->rgb444 = le_read16(e + 4); ev->reserved1 = le_read16(e + 6);
        if (!ev->line || ev->line >= GFX_HEIGHT || ev->line % band ||
            ev->index >= OSRI_COLORS || ev->reserved0 || ev->reserved1 || ev->rgb444 > 0xfff ||
            (!OSRI_ALLOW_SAME(version) && current[ev->index] == ev->rgb444) ||
            (i && (ev->line < image->event[i-1].line ||
#ifdef SPLASH_RASTER_OBS
                   (version != OSRI_VERSION_2 &&
                    ev->line == image->event[i-1].line && ev->index <= image->event[i-1].index)
#else
                   (ev->line == image->event[i-1].line && ev->index <= image->event[i-1].index)
#endif
                   ))) goto done;
        line_count = i && ev->line == image->event[i-1].line ? line_count + 1 : 1;
#ifdef SPLASH_RASTER_OBS
        if (line_count == 1) seen = 0;
        if (seen & (1u << ev->index)) goto done;
        seen |= 1u << ev->index;
        if (version == OSRI_VERSION_2 && line_count > OBS_V2_MAX_EVENTS_PER_LINE) goto done;
#else
        if (version == OSRI_VERSION_2 && line_count > OSRI_V2_MAX_EVENTS_PER_LINE) goto done;
#endif
        current[ev->index] = ev->rgb444;
    }
    if (vfs_read_fd(fd, &extra, 1) != 0 || crc32_core_final(crc) != expected) goto done;
    result = 0;
done:
    vfs_close(fd);
    return result;
}
