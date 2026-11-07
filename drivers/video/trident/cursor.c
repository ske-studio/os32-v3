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
 * Author:  Alan Hourihane, alanh@fairlite.demon.co.uk
 */

/* 9682 cursor: C=MIT trident_dac.c:1046-1214, design §4.
 * Manufacturer reference: NEC LCD172VXM/192VXM manual p.20 (sync);
 * the 9682 register manual is unavailable, as recorded in the design.
 */
#include "trident.h"
#include "gfx_hal.h"

/* C:1066-1122,1046-1062: indexed CRTC registers and 64x64 controls. */
#define CUR_X_LO 0x40u
#define CUR_Y_LO 0x42u
#define CUR_ADDRESS_LO 0x44u
#define CUR_X_ORIGIN 0x46u
#define CUR_Y_ORIGIN 0x47u
#define CUR_FG 0x48u
#define CUR_BG 0x4Cu
#define CUR_CONTROL 0x50u
#define CUR_SHOW_64 0xC1u
#define CUR_HIDE_64 0x41u
#define CUR_POS_HIGH_MASK 0x0Fu
/* C:1199-1201: swapped mask/source, 32-bit interleave, MSB first. */
#define CUR_PLANE_WORD_BYTES (BITS_PER_U32 / BITS_PER_BYTE)
#define CUR_PAIR_BYTES (2u * CUR_PLANE_WORD_BYTES)
#define CUR_ROW_BYTES (TRI_CURSOR_WIDTH / BITS_PER_BYTE * 2u)
#define ARGB_ALPHA_SHIFT 24u
#define RGB_RED_SHIFT 16u
#define RGB_GREEN_SHIFT 8u
#define RGB_MASK 0x00FFFFFFu
#define DAC_MAX 63u /* VGA palette: six bits per channel. */
#define TRY(op) do { int rc_ = (op); if (rc_) return rc_; } while (0)

static bool cursor_supported(const struct trident_device *d)
{
    return d->cursor.available && !d->settings.sw_cursor &&
           !(d->mode.mux && d->mode.selected.bpp == 8);
}

int tri_cursor_init(struct trident_device *d)
{
    int rc;
    if (!d) return TRI_E_INVAL;
    d->cursor.available = false;
    if (d->settings.sw_cursor) return TRI_OK;
    if (!d->cursor.allocation.allocated) {
        /* F5 rounds this reservation to ceil(16384/pitch) whole rows.
         * C:1157-1177: the reservation is distinct from the 1024-byte image. */
        rc = tri_vram_alloc(d, &d->cursor.allocation, TRI_VRAM_CURSOR,
                            TRI_CURSOR_RESERVE_BYTES, TRI_CURSOR_ALIGN);
        if (rc) return TRI_OK; /* C:1166-1171, D:3700-3703: SW fallback. */
    }
    if (d->cursor.allocation.off >= TRI_CURSOR_OFFSET_LIMIT) {
        TRY(tri_vram_free(d, &d->cursor.allocation));
        return TRI_OK; /* C:1180-1190: optional cursor only. */
    }
    d->cursor.available = true;
    /* No initial Hide: C installs callbacks without touching CR50. */
    return TRI_OK;
}

/* X cursor-framework equivalent: inputs are RGB, output is a display pixel.
 * Expand six-bit DAC channels to eight bits before squared RGB distance.
 * Strict < retains the first palette entry on ties; no DAC I/O here. */
static u32 cursor_pixel(const struct trident_device *d, u32 rgb)
{
    u32 r = (rgb >> RGB_RED_SHIFT) & U8_MAX;
    u32 g = (rgb >> RGB_GREEN_SHIFT) & U8_MAX;
    u32 b = rgb & U8_MAX;
    if (d->mode.selected.bpp == 8) {
        u32 best = 0, distance = U32_MAX;
        for (u32 i = 0; i < TRI_DAC_COLORS; ++i) {
            i32 delta[3];
            u32 channel[3] = {r, g, b};
            for (u32 c = 0; c < 3; ++c) {
                u32 v = d->dac[i][c] & DAC_MAX;
                delta[c] = (i32)channel[c] - (i32)((v << 2) | (v >> 4));
            }
            u32 next = (u32)(delta[0] * delta[0] + delta[1] * delta[1] +
                             delta[2] * delta[2]);
            if (next < distance) {
                distance = next;
                best = i;
            }
        }
        return best;
    }
    if (d->mode.selected.depth == 15)
        return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
    if (d->mode.selected.depth == 16)
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    return rgb & RGB_MASK; /* packed24 and XRGB8888: B/G/R/0. */
}

int tri_cursor_convert(struct trident_device *d, const struct GfxCursorImage *im)
{
    u32 colors[2] = {0, 0}, count = 0;
    if (!d || !im || !im->argb_pm) return TRI_E_INVAL;
    if (!cursor_supported(d)) return TRI_E_NOSYS;
    if (!im->width || !im->height || im->width > TRI_CURSOR_WIDTH ||
        im->height > TRI_CURSOR_HEIGHT) return TRI_E_NOSYS;
    /* Bound each row before pointer arithmetic, including the final row. */
    u32 row_bytes = im->width * sizeof(u32);
    if (im->pitch < row_bytes || im->bytes < row_bytes ||
        im->height - 1u > (im->bytes - row_bytes) / im->pitch)
        return TRI_E_INVAL;
    /* Validate first, so unsupported alpha/third color preserves old scratch. */
    for (u32 y = 0; y < im->height; ++y) {
        const u8 *row = (const u8 *)im->argb_pm + y * im->pitch;
        for (u32 x = 0; x < im->width; ++x) {
            const u8 *p = row + x * sizeof(u32);
            u32 alpha = p[ARGB_ALPHA_SHIFT / BITS_PER_BYTE];
            if (!alpha) continue;
            if (alpha != U8_MAX) return TRI_E_NOSYS;
            u32 rgb = (u32)p[0] | ((u32)p[1] << RGB_GREEN_SHIFT) |
                      ((u32)p[2] << RGB_RED_SHIFT);
            if (count && rgb == colors[0]) continue;
            if (count > 1 && rgb == colors[1]) continue;
            if (count == 2) return TRI_E_NOSYS;
            colors[count++] = rgb;
        }
    }
    if (count == 1) colors[1] = colors[0];
    for (u32 i = 0; i < TRI_CURSOR_IMAGE_BYTES; ++i) d->cursor.image[i] = 0;
    for (u32 y = 0; y < im->height; ++y) {
        const u8 *row = (const u8 *)im->argb_pm + y * im->pitch;
        for (u32 x = 0; x < im->width; ++x) {
            const u8 *p = row + x * sizeof(u32);
            if (!p[ARGB_ALPHA_SHIFT / BITS_PER_BYTE]) continue;
            u32 rgb = (u32)p[0] | ((u32)p[1] << RGB_GREEN_SHIFT) |
                      ((u32)p[2] << RGB_RED_SHIFT);
            u32 byte = y * CUR_ROW_BYTES + (x / BITS_PER_U32) * CUR_PAIR_BYTES +
                       (x % BITS_PER_U32) / BITS_PER_BYTE;
            u8 bit = (u8)(1u << (BITS_PER_BYTE - 1u - x % BITS_PER_BYTE));
            d->cursor.image[byte] |= bit; /* swapped: mask word first */
            if (rgb == colors[1]) d->cursor.image[byte + CUR_PLANE_WORD_BYTES] |= bit;
        }
    }
    d->cursor.bg = cursor_pixel(d, colors[0]);
    d->cursor.fg = cursor_pixel(d, colors[1]);
    return TRI_OK;
}

int tri_cursor_colors(struct trident_device *d, u32 bg, u32 fg)
{
    if (!d) return TRI_E_INVAL;
    if (!cursor_supported(d)) return TRI_E_NOSYS;
    /* C:1091-1103: all foreground bytes before any background bytes. */
    for (u32 byte = 0; byte < sizeof(u32); ++byte)
        TRY(trident_indexed_write(d, d->cur_vga_base, CUR_FG + byte,
                                   (u8)(fg >> (byte * BITS_PER_BYTE))));
    for (u32 byte = 0; byte < sizeof(u32); ++byte)
        TRY(trident_indexed_write(d, d->cur_vga_base, CUR_BG + byte,
                                   (u8)(bg >> (byte * BITS_PER_BYTE))));
    return TRI_OK;
}

int tri_cursor_image(struct trident_device *d, const struct GfxCursorImage *im)
{
    if (!d || !im) return TRI_E_INVAL;
    if (!cursor_supported(d)) return TRI_E_NOSYS;
    /* Conversion is a separate HAL step; this callback loads its scratch.
     * Sync has a fresh operation-local deadline in F1, never retained here. */
    TRY(tri_sync(d));
    for (u32 i = 0; i < TRI_CURSOR_IMAGE_BYTES; i += sizeof(u32)) {
        const u8 *p = &d->cursor.image[i];
        u32 word = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
                   ((u32)p[3] << 24);
        if (d->res->ram_fallback) {
            for (u32 byte = 0; byte < sizeof(u32); ++byte)
                TRY(trident_write8(d, TRI_SPACE_LFB, d->cursor.allocation.off + i + byte, p[byte]));
        } else TRY(trident_write32(d, TRI_SPACE_LFB, d->cursor.allocation.off + i, word));
    }
    u32 address = d->cursor.allocation.off / TRI_CURSOR_ALIGN;
    TRY(trident_indexed_write(d, d->cur_vga_base, CUR_ADDRESS_LO, (u8)address));
    return trident_indexed_write(d, d->cur_vga_base, CUR_ADDRESS_LO + 1u,
                                  (u8)(address >> BITS_PER_BYTE));
}

int tri_cursor_move(struct trident_device *d, i32 x, i32 y)
{
    if (!d) return TRI_E_INVAL;
    if (!cursor_supported(d)) return TRI_E_NOSYS;
    /* Unsigned negation preserves the reference's low byte even at INT_MIN.
     * Hotspot subtraction belongs to the HAL caller, per the frozen contract. */
    TRY(trident_indexed_write(d, d->cur_vga_base, CUR_X_ORIGIN,
                               x < 0 ? (u8)(0u - (u32)x) : 0));
    if (x < 0) x = 0;
    TRY(trident_indexed_write(d, d->cur_vga_base, CUR_Y_ORIGIN,
                               y < 0 ? (u8)(0u - (u32)y) : 0));
    if (y < 0) y = 0;
    TRY(trident_indexed_write(d, d->cur_vga_base, CUR_X_LO, (u8)x));
    TRY(trident_indexed_write(d, d->cur_vga_base, CUR_X_LO + 1u,
                               (u8)(((u32)x >> BITS_PER_BYTE) & CUR_POS_HIGH_MASK)));
    TRY(trident_indexed_write(d, d->cur_vga_base, CUR_Y_LO, (u8)y));
    return trident_indexed_write(d, d->cur_vga_base, CUR_Y_LO + 1u,
                                  (u8)(((u32)y >> BITS_PER_BYTE) & CUR_POS_HIGH_MASK));
}

int tri_cursor_show(struct trident_device *d, bool show)
{
    if (!d) return TRI_E_INVAL;
    if (!cursor_supported(d)) return TRI_E_NOSYS;
    return trident_indexed_write(d, d->cur_vga_base, CUR_CONTROL,
                                  show ? CUR_SHOW_64 : CUR_HIDE_64);
}
