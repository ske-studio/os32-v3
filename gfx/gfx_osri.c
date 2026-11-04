#include "gfx_osri.h"
#include "gfx_hal.h"
#include "vfs.h"
#include "os32_kapi_shared.h"
#include "../lib/le_internal.h"
#include "../lib/crc32_core.inc"

#define OSRI_READ_CHUNK 4096u
#define OSRI_CRC_OFF 48u
static int read_part(int fd, u8 *p, u32 n, u32 *crc)
{
    while (n) {
        u32 size = n > OSRI_READ_CHUNK ? OSRI_READ_CHUNK : n;
        int got = vfs_read_fd(fd, p, size);
        if (got <= 0 || (u32)got > size) return OS32_ERR_IO;
        *crc = crc32_core_update(*crc, p, (u32)got);
        p += got;
        n -= (u32)got;
    }
    return 0;
}

int osri_load_checked(const char *path, struct gfx_kernel_fb *dst, struct OsriImage *image)
{
    u8 h[OSRI_HEADER_BYTES], pal[OSRI_COLORS * 2], e[OSRI_EVENT_BYTES], extra;
    u16 current[OSRI_COLORS], version;
    u32 crc = CRC32_INIT, expected, count, band, plane, i, line_count = 0;
    u16 seen = 0;
    int fd, result = OS32_ERR_INVAL;
    if (!path || !dst || !image || dst->width != GFX_WIDTH ||
        dst->height != GFX_HEIGHT || dst->pitch != GFX_BPL ||
        dst->format != GFX_BB_PLANAR4) return OS32_ERR_INVAL;
    plane = dst->pitch * dst->height;
    if (plane != GFX_PLANE_SZ || !dst->planes[0]) return OS32_ERR_INVAL;
    for (i = 1; i < GVRAM_PLANES; i++)
        if (dst->planes[i] != dst->planes[0] + i * plane) return OS32_ERR_INVAL;
    fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        if (fd == VFS_ERR_NOTFOUND) return OS32_ERR_NOTFOUND;
        if (fd == VFS_ERR_NOSPC) return OS32_ERR_NOSPC;
        if (fd == VFS_ERR_INVAL) return OS32_ERR_INVAL;
        return OS32_ERR_IO;
    }
    /* Header CRC field is zeroed before including it in the stream. */
    if (read_part(fd, h, sizeof(h), &crc)) { result = OS32_ERR_IO; goto done; }
    expected = le_read32(h + OSRI_CRC_OFF);
    for (i = OSRI_CRC_OFF; i < OSRI_CRC_OFF + 4; i++) h[i] = 0;
    crc = crc32_core_update(CRC32_INIT, h, sizeof(h));
    count = le_read32(h + 36); band = le_read16(h + 44);
    version = le_read16(h + 4);
    /* Bound the version's count before computing the exact file length. */
    if ((version != OSRI_VERSION_1 && version != OSRI_VERSION_2) ||
        count > (version == OSRI_VERSION_1 ? OSRI_V1_MAX_EVENTS : OSRI_V2_MAX_EVENTS)) goto done;
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
    if (read_part(fd, pal, sizeof(pal), &crc)) { result = OS32_ERR_IO; goto done; }
    for (i = 0; i < OSRI_COLORS; i++) {
        current[i] = image->pal[i] = le_read16(pal + 2 * i);
        if (current[i] > 0xfff) goto done;
    }
    for (i = 0; i < GVRAM_PLANES; i++)
        if (read_part(fd, dst->planes[i], plane, &crc)) { result = OS32_ERR_IO; goto done; }
    image->count = (u16)count; image->band = (u16)band;
    image->version = version;
    for (i = 0; i < count; i++) {
        struct OsriEvent *ev = &image->event[i];
        if (read_part(fd, e, sizeof(e), &crc)) { result = OS32_ERR_IO; goto done; }
        ev->line = le_read16(e); ev->index = e[2]; ev->reserved0 = e[3];
        ev->rgb444 = le_read16(e + 4); ev->reserved1 = le_read16(e + 6);
        if (!ev->line || ev->line >= GFX_HEIGHT || ev->line % band ||
            ev->index >= OSRI_COLORS || ev->reserved0 || ev->reserved1 || ev->rgb444 > 0xfff ||
            (version != OSRI_VERSION_2 && current[ev->index] == ev->rgb444) ||
            (i && (ev->line < image->event[i-1].line ||
                   (version != OSRI_VERSION_2 &&
                    ev->line == image->event[i-1].line && ev->index <= image->event[i-1].index)
                   ))) goto done;
        line_count = i && ev->line == image->event[i-1].line ? line_count + 1 : 1;
        if (line_count == 1) seen = 0;
        if (seen & (1u << ev->index)) goto done;
        seen |= 1u << ev->index;
        if (version == OSRI_VERSION_2 && line_count > OSRI_V2_MAX_EVENTS_PER_LINE) goto done;
        current[ev->index] = ev->rgb444;
    }
    int tail = vfs_read_fd(fd, &extra, 1);
    if (tail < 0) { result = OS32_ERR_IO; goto done; }
    if (tail != 0 || crc32_core_final(crc) != expected) goto done;
    result = 0;
done:
    vfs_close(fd);
    return result;
}

/* Boot keeps its existing 0/-1 contract. */
int osri_load(const char *path, struct gfx_kernel_fb *dst, struct OsriImage *image)
{
    return osri_load_checked(path, dst, image) ? -1 : 0;
}
