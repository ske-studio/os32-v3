/*
 * Copyright 1992-2003 by Alan Hourihane, North Wales, UK.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Alan Hourihane not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Alan Hourihane makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * ALAN HOURIHANE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ALAN HOURIHANE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:  Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *
 * Trident accelerated options.
 */

/* 9682-only port of X13 trident_accel.c:97-181,226-561 and
 * trident_regs.h:147-189,265-287,350-434; D:3199-3211,3263-3276.
 * TRIDENT_FULL_DESIGN §§2-3 supplies the XAA surface/upload contract.
 * HAL clips before dispatch; coordinates below include the surface offset.
 */
#include "trident.h"
#include "drivers/char/serial.h"

/* R:147-189, 96xx access widths are retained at each call site. */
enum tri_ge_reg {
    GE_STATUS = 0x2120, GE_OPERMODE = 0x2122, GE_COMMAND = 0x2124,
    GE_FMIX = 0x2127, GE_DRAWFLAG = 0x2128, GE_FCOLOUR = 0x212c,
    GE_BCOLOUR = 0x2130, GE_PATLOC = 0x2134, GE_DEST = 0x2138,
    GE_SRC = 0x213c, GE_DIM = 0x2140, GE_STYLE = 0x2144, GE_CKEY = 0x2168
};
enum tri_ge_bits {
    GE_BUSY = 0x80, GE_NOCLIP = 0x100, GE_DST_ENABLE = 0x200,
    GE_BLT = 1, GE_BRESLINE = 4,
    GE_PAT2SCR = 0x0002, GE_SCR2SCR = 0x0004, GE_PATMONO = 0x0020,
    GE_YNEG = 0x0100, GE_XNEG = 0x0200, GE_YMAJ = 0x0400,
    GE_TRANSPARENT = 0x1000, GE_SOLIDFILL = 0x4000, GE_STENCIL = 0x8000
};
/* A:226-277: first report after 10M reads, then every 10K, reset on #8. */
#define GE_POLL_FIRST 10000000u
#define GE_POLL_RESTART 9990000u
#define GE_POLL_REPORTS 8u
#define GE_FORMAT_MASK 3u
#define GE_FORMAT_8 0u
#define GE_FORMAT_16 1u
#define GE_FORMAT_32 2u
/* D:3263-3276 uses BYTE pitch, including the 8192 alias for code zero. */
#define GE_PITCH_512_8192 0x00u
#define GE_PITCH_1024 0x04u
#define GE_PITCH_2048 0x08u
#define GE_PITCH_4096 0x0cu
/* XAA octants, as frozen in trident.h; R:294-296 XY_MERGE. */
#define LINE_YMAJOR 1u
#define LINE_XDECREASING 2u
#define LINE_YDECREASING 4u
#define LINE_DASH_BITS 16u
#define LINE_DASH_MASK 0xffffu
#define ROP_COUNT 16u

/* XAAGetCopyROP / XAAGetPatternROP: GXclear through GXset, ROP3. */
static const u8 copy_rop[ROP_COUNT] = {
    0x00, 0x88, 0x44, 0xcc, 0x22, 0xaa, 0x66, 0xee,
    0x11, 0x99, 0x55, 0xdd, 0x33, 0xbb, 0x77, 0xff
};
static const u8 pattern_rop[ROP_COUNT] = {
    0x00, 0xa0, 0x50, 0xf0, 0x0a, 0xaa, 0x5a, 0xfa,
    0x05, 0xa5, 0x55, 0xf5, 0x0f, 0xaf, 0x5f, 0xff
};
#define TRY(op) do { int rc_ = (op); if (rc_) return rc_; } while (0)

/* Count accesses actually dispatched, including failed device accesses. */
#define GE_WRITE(bits) \
static int ge_write##bits(struct trident_device *d, u32 reg, u##bits value) \
{ \
    if (!d || !d->io || !d->io->write##bits) return TRI_E_INVAL; \
    ++d->accel.io_accesses; \
    return trident_write##bits(d, TRI_SPACE_REGS, reg, value); \
}
GE_WRITE(8)
GE_WRITE(16)
GE_WRITE(32)

static u32 ge_xy(u32 x, u32 y)
{
    return (((u32)y & U16_MAX) << 16) | ((u32)x & U16_MAX);
}
static int ge_dim(struct trident_device *d, u32 w, u32 h)
{
    /* Also used for Bresenham's signed error/increment registers (R:391). */
    return ge_write32(d, GE_DIM, ge_xy(w - 1u, h - 1u));
}
static int ge_command(struct trident_device *d, u8 command)
{
    TRY(ge_write8(d, GE_COMMAND, command));
    ++d->accel.hw_ops;
    return TRI_OK;
}
static u32 ge_replicate(const struct trident_device *d, u32 color)
{
    switch (d->accel.engine_operation & GE_FORMAT_MASK) {
    case GE_FORMAT_8:
        color &= U8_MAX;
        color |= color << 8;
        color |= color << 16;
        break;
    case GE_FORMAT_16:
        color = ((color & U16_MAX) << 16) | (color & U16_MAX);
        break;
    }
    return color;
}

/* Both entry points use fresh local counters. No retained deadline/latch,
 * no pre-command wait, and ClearSync never restores OPERMODE (A:226-277). */
static int ge_poll(struct trident_device *d)
{
    u32 count = 0, timeout = 0;
    for (;;) {
        u8 status;
        if (!d->io || !d->io->read8) return TRI_E_INVAL;
        ++d->accel.io_accesses;
        TRY(trident_read8(d, TRI_SPACE_REGS, GE_STATUS, &status));
        if (!(status & GE_BUSY)) return TRI_OK;
        if (++count == GE_POLL_FIRST) {
            serial_puts("Trident: BitBLT engine time-out.\n");
            count = GE_POLL_RESTART;
            if (++timeout == GE_POLL_REPORTS)
                return ge_write8(d, GE_STATUS, 0);
        }
    }
}
int tri_sync(struct trident_device *d)
{
    if (!d) return TRI_E_INVAL;
    if (!d->accel.initialized) return TRI_OK;
    TRY(ge_write16(d, GE_OPERMODE, d->accel.engine_operation));
    return ge_poll(d);
}
int tri_clear_sync(struct trident_device *d)
{
    if (!d) return TRI_E_INVAL;
    if (!d->accel.initialized) return TRI_OK;
    return ge_poll(d);
}

int tri_accel_init(struct trident_device *d, const struct tri_mode *m)
{
    u16 operation;
    u32 pitch;
    int rc;
    if (!d || !m) return TRI_E_INVAL;
    d->accel.initialized = false;
    if (d->accel.pattern.allocated)
        TRY(tri_vram_free(d, &d->accel.pattern));
    d->accel.draw_flag = 0;
    d->accel.blt_scan_direction = 0;
    d->accel.clipping = false; /* A:101-111, no 9682 clip-window writes. */
    if (d->settings.no_accel || (m->flags & TRI_MODE_NOACCEL) || m->bpp == 24)
        return TRI_OK;
    switch (m->bpp) {
    case 8: operation = GE_FORMAT_8; break;
    case 16: operation = GE_FORMAT_16; break;
    case 32: operation = GE_FORMAT_32; break;
    default: return TRI_E_INVAL;
    }
    pitch = (u32)m->display_width * (m->bpp / BITS_PER_BYTE);
    switch (pitch) {
    case 512: case 8192: operation |= GE_PITCH_512_8192; break;
    case 1024: operation |= GE_PITCH_1024; break;
    case 2048: operation |= GE_PITCH_2048; break;
    case 4096: operation |= GE_PITCH_4096; break;
    default: return TRI_E_INVAL;
    }
    d->accel.engine_operation = operation | GE_NOCLIP;
    TRY(ge_write16(d, GE_OPERMODE, d->accel.engine_operation));
    d->accel.pattern_location = pitch;
    d->accel.initialized = true;
    /* XAA offscreen cache failure disables only mono patterns. Keep the
     * unallocated record as the availability indication; no global failure. */
    rc = tri_vram_alloc(d, &d->accel.pattern, TRI_VRAM_PATTERN,
                        TRI_PATTERN_BYTES, TRI_PATTERN_ALIGN);
    if (rc) return TRI_OK;
    return TRI_OK;
}

/* Convert a clipped, creation-validated, whole-row surface to GE coordinates.
 * The HAL owns clipping, so malformed nonempty rectangles are rejected here.
 * Empty/negative extents are handled before this helper without MMIO. */
static int ge_rect(struct trident_device *d, const struct tri_surface *s,
                   int *x, int *y, int w, int h)
{
    u32 row, cpp;
    if (!d || !s || !d->accel.initialized) return TRI_E_STATE;
    cpp = 1u << (d->accel.engine_operation & GE_FORMAT_MASK);
    if (!s->pitch || s->pitch != d->accel.pattern_location || s->cpp != cpp ||
        s->off % s->pitch || *x < 0 || *y < 0 || w <= 0 || h <= 0 ||
        *x >= s->width || *y >= s->height ||
        w > (int)s->width - *x || h > (int)s->height - *y)
        return TRI_E_SURFACE;
    row = s->off / s->pitch;
    if ((u32)*x + (u32)w - 1u > U16_MAX || row > TRI_GE_MAX_Y ||
        (u32)*y + (u32)h - 1u > TRI_GE_MAX_Y - row)
        return TRI_E_SURFACE;
    *y += (int)row;
    return TRI_OK;
}

int tri_copy_direction(struct trident_device *d, int xdir, int ydir)
{
    if (!d || (xdir != 1 && xdir != -1) || (ydir != 1 && ydir != -1))
        return TRI_E_INVAL;
    d->accel.blt_scan_direction = 0;
    if (xdir < 0) d->accel.blt_scan_direction |= GE_XNEG;
    if (ydir < 0) d->accel.blt_scan_direction |= GE_YNEG;
    return TRI_OK;
}
int tri_line_setup(struct trident_device *d, u32 fg, i32 bg, u8 rop,
                   u8 dash_length)
{
    if (!d || rop >= ROP_COUNT ||
        (dash_length != 0 && dash_length != 2 && dash_length != 4 &&
         dash_length != 8 && dash_length != 16)) return TRI_E_INVAL;
    d->accel.line_fg = fg;
    d->accel.line_bg = bg;
    d->accel.line_rop = rop;
    d->accel.dash_length = dash_length;
    return TRI_OK;
}

int tri_fill(struct trident_device *d, const struct tri_surface *s,
             int x, int y, int w, int h, u32 color, u8 rop)
{
    if (w <= 0 || h <= 0) return TRI_OK;
    if (rop >= ROP_COUNT) return TRI_E_INVAL;
    TRY(ge_rect(d, s, &x, &y, w, h));
    color = ge_replicate(d, color);
    TRY(ge_write8(d, GE_FMIX, pattern_rop[rop]));
    TRY(ge_write32(d, GE_FCOLOUR, color));
    TRY(ge_write32(d, GE_DRAWFLAG, d->accel.draw_flag | GE_SOLIDFILL | GE_PATMONO));
    TRY(ge_dim(d, w, h));
    TRY(ge_write32(d, GE_DEST, ge_xy(x, y)));
    TRY(ge_command(d, GE_BLT));
    return tri_sync(d);
}
int tri_copy(struct trident_device *d, const struct tri_surface *src,
             const struct tri_surface *dst, int sx, int sy, int dx, int dy,
             int w, int h, u8 rop, int transparent, u32 key)
{
    u32 direction;
    if (w <= 0 || h <= 0) return TRI_OK;
    if (rop >= ROP_COUNT) return TRI_E_INVAL;
    TRY(ge_rect(d, src, &sx, &sy, w, h));
    TRY(ge_rect(d, dst, &dx, &dy, w, h));
    direction = d->accel.blt_scan_direction & (GE_XNEG | GE_YNEG);
    /* A:292-303 compares AFTER REPLICATE, even at 8/16bpp. */
    key = ge_replicate(d, key);
    if (transparent && key != U32_MAX) {
        TRY(ge_write16(d, GE_OPERMODE, d->accel.engine_operation | GE_DST_ENABLE));
        TRY(ge_write32(d, GE_CKEY, key));
    }
    TRY(ge_write32(d, GE_DRAWFLAG, d->accel.draw_flag | direction | GE_SCR2SCR));
    TRY(ge_write8(d, GE_FMIX, copy_rop[rop]));
    if (direction & GE_YNEG) { sy += h - 1; dy += h - 1; }
    if (direction & GE_XNEG) { sx += w - 1; dx += w - 1; }
    TRY(ge_write32(d, GE_SRC, ge_xy(sx, sy)));
    TRY(ge_write32(d, GE_DEST, ge_xy(dx, dy)));
    TRY(ge_dim(d, w, h));
    TRY(ge_command(d, GE_BLT));
    return tri_clear_sync(d);
}

int tri_pattern_upload(struct trident_device *d, const u8 rows[8], u32 *off)
{
    if (!d || !rows || !off) return TRI_E_INVAL;
    if (!d->accel.initialized || !d->accel.pattern.allocated) return TRI_E_NOSYS;
    TRY(tri_sync(d));
    for (u32 row = 0; row < TRI_PATTERN_BYTES; ++row) {
        if (!d->io || !d->io->write8) return TRI_E_INVAL;
        ++d->accel.io_accesses;
        TRY(trident_write8(d, TRI_SPACE_LFB, d->accel.pattern.off + row, rows[row]));
    }
    *off = d->accel.pattern.off;
    return TRI_OK;
}
int tri_pattern(struct trident_device *d, const struct tri_surface *s,
                u32 pattern_off, int x, int y, int w, int h,
                u32 fg, i32 bg, u8 rop)
{
    u32 flag = 0;
    if (w <= 0 || h <= 0) return TRI_OK;
    if (rop >= ROP_COUNT) return TRI_E_INVAL;
    TRY(ge_rect(d, s, &x, &y, w, h));
    if (!d->accel.pattern.allocated) return TRI_E_NOSYS;
    if (pattern_off != d->accel.pattern.off || pattern_off % TRI_PATTERN_ALIGN)
        return TRI_E_INVAL;
    fg = ge_replicate(d, fg);
    TRY(ge_write32(d, GE_FCOLOUR, fg));
    if (bg == -1) {
        flag |= GE_TRANSPARENT;
        TRY(ge_write32(d, GE_BCOLOUR, ~fg));
    } else {
        TRY(ge_write32(d, GE_BCOLOUR, ge_replicate(d, (u32)bg)));
    }
    TRY(ge_write32(d, GE_DRAWFLAG, d->accel.draw_flag | GE_PAT2SCR | GE_PATMONO | flag));
    TRY(ge_write16(d, GE_PATLOC, (u16)(pattern_off >> 6)));
    TRY(ge_write8(d, GE_FMIX, pattern_rop[rop]));
    TRY(ge_write32(d, GE_DEST, ge_xy(x, y)));
    TRY(ge_dim(d, w, h));
    TRY(ge_command(d, GE_BLT));
    return tri_sync(d);
}

int tri_line(struct trident_device *d, const struct tri_surface *s, int x, int y,
             int dmaj, int dmin, int error, int len, u8 octant, u16 dash, u8 phase)
{
    u32 flag = 0, fg, style;
    u8 length;
    int w = 1, h = 1;
    if (len <= 0) return TRI_OK;
    if (!d || octant > (LINE_YMAJOR | LINE_XDECREASING | LINE_YDECREASING) ||
        len > (int)U16_MAX || dmaj < 0 || dmin < 0 || dmin > dmaj ||
        dmaj > (int)U16_MAX) return TRI_E_INVAL;
    length = d->accel.dash_length;
    if (!length && !dmin) {
        /* XAA's HorVert callback takes the top/left end. Convert the
         * directed line ABI before validating the complete rectangle. */
        if (octant & LINE_YMAJOR) {
            h = len;
            if (octant & LINE_YDECREASING) {
                if (y < len - 1) return TRI_E_SURFACE;
                y -= len - 1;
            }
        } else {
            w = len;
            if (octant & LINE_XDECREASING) {
                if (x < len - 1) return TRI_E_SURFACE;
                x -= len - 1;
            }
        }
    }
    /* XAA/HAL clips all Bresenham endpoints; axis lines use the full rect. */
    TRY(ge_rect(d, s, &x, &y, w, h));
    if (d->accel.line_rop >= ROP_COUNT) return TRI_E_INVAL;
    fg = ge_replicate(d, d->accel.line_fg);
    d->accel.blt_scan_direction = 0; /* A:333,409 resets copy direction. */
    if (!length) {
        TRY(ge_write8(d, GE_FMIX, pattern_rop[d->accel.line_rop]));
        TRY(ge_write32(d, GE_FCOLOUR, fg));
        if (!dmin) {
            /* A:367-379 has DIM before DEST and no STENCIL/PATMONO. */
            TRY(ge_write32(d, GE_DRAWFLAG, d->accel.draw_flag | GE_SOLIDFILL));
            TRY(ge_dim(d, w, h));
            TRY(ge_write32(d, GE_DEST, ge_xy(x, y)));
            TRY(ge_command(d, GE_BLT));
            return tri_sync(d);
        }
        flag = GE_SOLIDFILL;
    } else {
        if (length != 2 && length != 4 && length != 8 && length != 16)
            return TRI_E_INVAL;
        /* A:394-401, low length bits, repeated MSB-first to sixteen bits. */
        style = (u32)dash & ((1u << length) - 1u);
        for (u32 bits = length; bits < LINE_DASH_BITS; bits *= 2u)
            style |= style << bits;
        TRY(ge_write32(d, GE_FCOLOUR, fg));
        if (d->accel.line_bg == -1) {
            flag |= GE_TRANSPARENT;
            TRY(ge_write32(d, GE_BCOLOUR, ~fg));
        } else {
            TRY(ge_write32(d, GE_BCOLOUR, ge_replicate(d, (u32)d->accel.line_bg)));
        }
        TRY(ge_write8(d, GE_FMIX, pattern_rop[d->accel.line_rop]));
        phase %= LINE_DASH_BITS;
        style = ((style >> phase) | (style << (LINE_DASH_BITS - phase))) & LINE_DASH_MASK;
        TRY(ge_write32(d, GE_STYLE, style));
    }
    if (octant & LINE_YMAJOR) flag |= GE_YMAJ;
    if (octant & LINE_XDECREASING) flag |= GE_XNEG;
    if (octant & LINE_YDECREASING) flag |= GE_YNEG;
    TRY(ge_write32(d, GE_DRAWFLAG, d->accel.draw_flag | GE_STENCIL | flag));
    TRY(ge_write32(d, GE_SRC, ge_xy(dmin - dmaj, dmin)));
    TRY(ge_write32(d, GE_DEST, ge_xy(x, y)));
    TRY(ge_dim(d, (u32)dmin + (u32)error, (u32)len));
    TRY(ge_command(d, GE_BRESLINE));
    return tri_sync(d);
}
