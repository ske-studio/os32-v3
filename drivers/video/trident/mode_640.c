/* Source notices retained from trident_mode.hwb:3–48.
 * Copyright 1991-1999 by The XFree86 Project, Inc.
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * vgaHW.c license: xorg-server-21.1.13 COPYING:54-76
 * Copyright (C) 1994-2003 The XFree86 Project, Inc.  All Rights Reserved.
 * Copyright (C) Colin Harrison 2005-2008
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
 * NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall not
 * be used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from the XFree86 Project.
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
 */
/* 9682 mode images: MIT trident_driver-1.4.0.c:952-988,1691-1711,
 * 3057-3100,3155-3162; trident_dac.c:253-271,564-740,779-908;
 * trident_pll.c:42-135; vgaHW.c:1185-1353,1391-1468.
 * Manufacturer sync reference: NEC LCD172VXM/192VXM manual p.20;
 * 9682 datasheet unavailable. PC-98 routing/FIFO/MCLK remain in pc98_init
 * and route_ra266. Enable belongs to lifecycle, not ModeInit (D:1043-1044).
 * PC-98 skips WAITFORVSYNC (D:948); no PLL wait or invented retry limit.
 */
#include "trident.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define TRY(expr) do { int rc = (expr); if (rc) return rc; } while (0)
/* D:3057, ClockLimit[16/24/32bpp] PROVIDIA9682 column. */
#define MODE_MIN_CLOCK_KHZ 12000u
#define MODE_MAX_CLOCK_KHZ 135000u
#define MODE_MAX_TRUECOLOR_KHZ 70000u
/* Design §5: only the 31kHz output band, before MUX/32bpp PLL scaling. */
#define MODE_MIN_HZ 31000u
#define MODE_MAX_HZ 32000u
#define MODE_PLL_ERROR_KHZ 750u /* P:62, strict improvement */
#define MODE_PLL_N_MAX 121u
#define MODE_PLL_M_MAX 31u
#define MODE_PLL_K_THRESHOLD 50000u
#define MODE_MISC_BASE 0x23u /* vgaHWInit, explicit sync polarities */
#define MODE_CLOCK_SELECT 0x08u /* C:636: (MiscOutReg & F3) | 08 */
#define MODE_CRTC_COUNT 25u
#define MODE_MAX_VTOTAL 2048u /* C:261-265: vertical bit10 */
#define MODE_MAX_HTOTAL 4095u /* C:267-271: horizontal bit11 */
#define MODE_FIRST_GE_PITCH 512u /* D:1691, increasing powers of two */
#define MODE_LAST_GE_PITCH 4096u
#define MODE_NTSC_SIG 15013700u /* R:27, binary32 14.31818 MHz */
#define MODE_PAL_SIG 9297975u   /* R:28, binary32 17.73448 MHz */
#define MODE_SIG_MIN 0x800000u /* binary32's implicit significand bit */
#define MODE_SIG_LIMIT 0x1000000u

/* F0 IDs: timing-major, format-minor. display_width is selected in prepare.
 * Monitor timings are OS32 inputs (design §5), not Trident built-in modes. */
#define MODE_ROW(t, v, vs, ve, vt, polarity) \
    {1u+(t)*TRI_FORMAT_COUNT+TRI_FMT_INDEX8, 25175, \
     640,656,752,800,v,vs,ve,vt,0,8,8,polarity}, \
    {1u+(t)*TRI_FORMAT_COUNT+TRI_FMT_RGB555, 25175, \
     640,656,752,800,v,vs,ve,vt,0,15,16,polarity}, \
    {1u+(t)*TRI_FORMAT_COUNT+TRI_FMT_RGB565, 25175, \
     640,656,752,800,v,vs,ve,vt,0,16,16,polarity}, \
    {1u+(t)*TRI_FORMAT_COUNT+TRI_FMT_PACKED24, 25175, \
     640,656,752,800,v,vs,ve,vt,0,24,24,(polarity)|TRI_MODE_NOACCEL}, \
    {1u+(t)*TRI_FORMAT_COUNT+TRI_FMT_XRGB8888, 25175, \
     640,656,752,800,v,vs,ve,vt,0,24,32,polarity}
const struct tri_mode tri_modes[TRI_MODE_COUNT] = {
    MODE_ROW(0,480,490,492,525,TRI_MODE_NHSYNC|TRI_MODE_NVSYNC),
    MODE_ROW(1,400,412,414,449,TRI_MODE_NHSYNC),
    MODE_ROW(2,350,387,389,449,TRI_MODE_NVSYNC)
};
#undef MODE_ROW

static const u8 mode_sr[] = { 0x01, 0x0F, 0x00, 0x0E }; /* SR1..4 */
static const u8 mode_gr[] = { 0, 0, 0, 0, 0, 0x40, 0x05, 0x0F, 0xFF };
static const u8 mode_ar[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    0x41, 0, 0x0F, 0, 0
};

/* Positive binary32 arithmetic for P:93, without kernel floating point.
 * Each operation rounds to nearest/even, then the final kHz truncates to int.
 * NTSC/PAL are the stored floats for 14318180/17734480 Hz (R:27-28):
 * 0x41651744 = 15013700 * 2^-20, 0x418de037 = 9297975 * 2^-19.
 * All intermediates fit u32: multiplier <=129 (or 125), divisor <=66.
 * No 64-bit division or floating-point runtime imports are needed. */
static u32 mode_round_shift(u32 value, u32 shift)
{
    if (!shift) return value;
    u32 q = value >> shift;
    u32 rem = value & ((1u << shift) - 1u);
    u32 half = 1u << (shift - 1u);
    return q + (rem > half || (rem == half && (q & 1u)));
}

static u32 mode_normalize(u32 value, i32 *exponent)
{
    u32 shift = 0;
    for (u32 top = value; top >= MODE_SIG_LIMIT; top >>= 1) ++shift;
    value = mode_round_shift(value, shift);
    *exponent += (i32)shift;
    if (value == MODE_SIG_LIMIT) {
        value >>= 1;
        ++*exponent;
    }
    return value;
}

static u32 mode_pll_frequency(bool pal, u32 n, u32 m, u32 k)
{
    i32 exponent = pal ? -19 : -20;
    u32 sig = pal ? MODE_PAL_SIG : MODE_NTSC_SIG;
    sig = mode_normalize(sig * (n + 8u), &exponent);
    u32 divisor = (m + 2u) << k;
    while (sig < divisor * MODE_SIG_MIN) {
        sig <<= 1;
        --exponent;
    }
    u32 q = sig / divisor, rem = sig % divisor;
    q += rem * 2u > divisor || (rem * 2u == divisor && (q & 1u));
    sig = mode_normalize(q, &exponent);
    /* *1000 == *125 followed by an exact exponent increase of three. */
    sig = mode_normalize(sig * 125u, &exponent);
    exponent += 3;
    return exponent < 0 ? sig >> -exponent : sig << exponent;
}

static int mode_clock(u32 clock, bool pal, u8 pll[2])
{
    u32 diff = MODE_PLL_ERROR_KHZ;
    bool found = false;
    for (u32 k = clock > MODE_PLL_K_THRESHOLD ? 1u : 0u; k <= 1; ++k) {
        for (u32 n = 0; n <= MODE_PLL_N_MAX; ++n) {
            for (u32 m = 1; m <= MODE_PLL_M_MAX; ++m) {
                u32 freq = mode_pll_frequency(pal, n, m, k);
                u32 error = freq > clock ? freq - clock : clock - freq;
                if (error < diff) {
                    diff = error;
                    pll[0] = ((m & 1u) << 7) | n;
                    pll[1] = (m >> 1) | (k << 4);
                    found = true;
                }
            }
        }
    }
    return found ? TRI_OK : TRI_E_INVAL;
}

static bool mode_mux(const struct trident_device *d, const struct tri_mode *m)
{
    u32 threshold = d->settings.mux_threshold_khz;
    if (!threshold) threshold = TRI_MUX_THRESHOLD_KHZ;
    return m->dot_khz > threshold;
}

static int mode_format(const struct tri_mode *m)
{
    for (u32 i = 0; i < TRI_FORMAT_COUNT; ++i)
        if (m->depth == tri_format_caps[i].depth &&
            m->bpp == tri_format_caps[i].bpp) return (int)i;
    return TRI_E_INVAL;
}

int tri_mode_prepare(struct trident_device *d, const struct tri_mode *candidate,
                     struct tri_mode *prepared)
{
    if (!d || !candidate || !prepared) return TRI_E_INVAL;
    struct tri_mode m = *candidate;
    int format = mode_format(&m);
    if (format < 0) return format;
    if (m.flags & ~(TRI_MODE_NHSYNC | TRI_MODE_NVSYNC |
                    TRI_MODE_DOUBLESCAN | TRI_MODE_NOACCEL)) return TRI_E_INVAL;
    if (!m.hd || m.hd >= m.hs || m.hs >= m.he || m.he >= m.ht ||
        !m.vd || m.vd >= m.vs || m.vs >= m.ve || m.ve >= m.vt)
        return TRI_E_INVAL;
    /* D's validation bounds: at most 2048 virtual lines; C:261-271
     * carries bit10 vertically and bit11 of horizontal total/blank start. */
    u32 vscale = m.flags & TRI_MODE_DOUBLESCAN ? 2u : 1u;
    if (m.vt * vscale > MODE_MAX_VTOTAL || m.ht > MODE_MAX_HTOTAL ||
        ((m.hd | m.hs | m.he | m.ht) & 7u)) return TRI_E_INVAL;
    u32 max_clock = m.bpp > 16 ? MODE_MAX_TRUECOLOR_KHZ : MODE_MAX_CLOCK_KHZ;
    if (m.dot_khz < MODE_MIN_CLOCK_KHZ || m.dot_khz > max_clock ||
        m.dot_khz * 1000u < MODE_MIN_HZ * m.ht ||
        m.dot_khz * 1000u > MODE_MAX_HZ * m.ht) return TRI_E_INVAL;
    if (d->settings.no_accel || d->settings.ram_fallback || m.bpp == 24) m.flags |= TRI_MODE_NOACCEL;
    if (m.flags & TRI_MODE_NOACCEL) {
        m.display_width = m.hd; /* already an eight-pixel multiple */
    } else {
        /* D:GetAccelPitchValues: choose the first fitting pixel pitch. */
        u32 width = MODE_FIRST_GE_PITCH;
        while (width < m.hd && width < MODE_LAST_GE_PITCH) width <<= 1;
        m.display_width = width;
    }
    struct tri_surface display = {
        .off = 0, .pitch = m.display_width * (m.bpp / 8u),
        .width = m.hd, .height = m.vd, .cpp = m.bpp / 8u, .format = format
    };
    display.bytes = display.pitch * display.height;
    TRY(tri_surface_validate(d, &display));
    u8 pll[2];
    u32 clock = m.dot_khz * (m.bpp == 32 ? 2u : 1u);
    /* An unset reference is read only by ModeInit after PC98 Init. Do not
     * reject a candidate against the other board's oscillator; set performs
     * the definitive PLL search once IsClearTV has cached the actual input. */
    if (d->reference)
        TRY(mode_clock(clock, (d->reference & 0x80u) != 0, pll));
    *prepared = m;
    return TRI_OK;
}

/* vgaHWInit + KGA_ENABLE_ON_ZERO. Blank starts at display and ends at
 * total, as in X's derived Crtc timings. MUX scales a local image only. */
static void mode_crtc(const struct tri_mode *mode, bool mux, u8 cr[MODE_CRTC_COUNT])
{
    u32 hd = mode->hd, hs = mode->hs, he = mode->he, ht = mode->ht;
    if (mux && mode->bpp == 8) { hd >>= 1; hs >>= 1; he >>= 1; ht >>= 1; }
    u32 vd = mode->vd, vs = mode->vs, ve = mode->ve, vt = mode->vt;
    if (mode->flags & TRI_MODE_DOUBLESCAN) { vd *= 2; vs *= 2; ve *= 2; vt *= 2; }
    for (u32 i = 0; i < MODE_CRTC_COUNT; ++i) cr[i] = 0;
    cr[0] = (ht >> 3) - 5;
    cr[1] = (hd >> 3) - 1;
    cr[2] = (hd >> 3) - 1;
    i32 hb = ((ht >> 3) - 1) & 0x3F;
    if (hb-- > (i32)(((hd >> 3) - 1) & 0x3F)) hb = 0;
    cr[3] = (hb & 0x1F) | 0x80;
    cr[4] = hs >> 3;
    cr[5] = ((hb & 0x20) << 2) | ((he >> 3) & 0x1F);
    cr[6] = vt - 2;
    cr[7] = (((vt-2)&0x100)>>8) | (((vd-1)&0x100)>>7) |
            ((vs&0x100)>>6) | (((vd-1)&0x100)>>5) | 0x10 |
            (((vt-2)&0x200)>>4) | (((vd-1)&0x200)>>3) | ((vs&0x200)>>2);
    cr[9] = (((vd-1)&0x200)>>4) | 0x40;
    if (mode->flags & TRI_MODE_DOUBLESCAN) cr[9] |= 0x80;
    cr[16] = vs;
    cr[17] = (ve & 0x0F) | 0x20;
    cr[18] = vd - 1;
    cr[19] = mode->display_width >> 4; /* vgaHW guess, then extended offset */
    cr[21] = vd - 1;
    i32 vb = (vt - 1) & 0xFF;
    if (vb > (i32)((vd-1)&0xFF) &&
        (vb & 0x7F) > (i32)((vd-1)&0x7F) && !(cr[9] & 0x9F)) vb = 0;
    else --vb;
    cr[22] = vb;
    cr[23] = 0xC3;
    cr[24] = 0xFF;
}

/* vgaHW's writeSeq/writeCrtc/writeGr are OUTB pairs, unlike OUTW_3x4. */
static int vga_write(struct trident_device *d, u16 port, u8 index, u8 value)
{
    TRY(trident_write8(d, TRI_SPACE_REGS, port, index));
    return trident_write8(d, TRI_SPACE_REGS, port + 1, value);
}

static int palette_access(struct trident_device *d, bool enable)
{
    u8 ignored;
    TRY(trident_read8(d, TRI_SPACE_REGS, TRI_CR_COLOR + 6, &ignored));
    return trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, enable ? 0 : 0x20);
}

int tri_mode_set(struct trident_device *d, const struct tri_mode *candidate)
{
    u8 ignored, misc_ext, interface, performance, addcol, misc_int, pci;
    struct tri_mode m;
    TRY(tri_mode_prepare(d, candidate, &m));
    bool mux = mode_mux(d, &m);
    bool no_accel = (m.flags & TRI_MODE_NOACCEL) != 0;
    u8 cr[MODE_CRTC_COUNT], pll[2];
    mode_crtc(&m, mux, cr);
    u8 misc = MODE_MISC_BASE;
    if (m.flags & TRI_MODE_NHSYNC) misc |= 0x40;
    if (m.flags & TRI_MODE_NVSYNC) misc |= 0x80;
    u32 offset = m.display_width * (m.bpp / 8u) >> 3;
    u8 dac = 0, pixel_bus = 0, newmode2 = 0x20, ext = 0x02;
    if (m.bpp == 16) { dac = m.depth == 15 ? 0x10 : 0x30; pixel_bus = 0x05; }
    if (m.bpp == 24) { dac = 0xD0; pixel_bus = 0x29; }
    if (m.bpp == 32) { dac = 0xD0; pixel_bus = 0x09; ext |= 0x08; }
    if (mux && m.bpp == 8) { pixel_bus |= 0x01; newmode2 |= 0x02; dac |= 0x20; }
    u32 vt = m.vt, vd = m.vd, vs = m.vs;
    if (m.flags & TRI_MODE_DOUBLESCAN) { vt *= 2; vd *= 2; vs *= 2; }
    u8 hiord = (((vt-1)&0x400)>>4) | (((vt-2)&0x400)>>3) |
               ((vs&0x400)>>5) | (((vd-1)&0x400)>>6) | 0x08;
    u32 ht = m.ht, hd = m.hd;
    if (mux && m.bpp == 8) { ht >>= 1; hd >>= 1; }
    u8 hover = ((ht&0x800)>>11) | ((hd&0x800)>>7);
    u16 base = d->cur_vga_base;

    /* D:1004; TridentInit prepares its image before vgaHWProtect. */
    TRY(trident_crtc_lock(d, base, false));
    d->active = true; /* D:1010 vtSema, after CRTC unlock. */
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0B, &ignored));
    TRY(trident_indexed_read(d, TRI_GR_INDEX, 0x0F, &misc_ext));
    /* IsClearTV caches frequency, including NTSC (CR C0 bit7 clear).
     * reference=0 is unset; bit0 marks a cached selection, bit7 is PAL. */
    if (d->reference == 0) {
        TRY(trident_indexed_read(d, base, 0xC0, &ignored));
        d->reference = (ignored & 0x80) | 1;
    }
    TRY(mode_clock(m.dot_khz * (m.bpp == 32 ? 2u : 1u),
                   (d->reference & 0x80) != 0, pll));
    TRY(trident_indexed_read(d, base, 0x2A, &interface));
    TRY(trident_indexed_read(d, base, 0x2F, &performance));
    TRY(trident_write8(d, TRI_SPACE_REGS, base, 0x3A));
    TRY(trident_indexed_read(d, base, 0x29, &addcol));
    TRY(trident_indexed_read(d, TRI_GR_INDEX, 0x2F, &misc_int));
    TRY(trident_indexed_read(d, base, 0x39, &pci));
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x20, &ignored));
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x12, &ignored));

    TRY(trident_protect(d, base, true));
    d->cur_vga_base = TRI_CR_COLOR; /* vgaHW.c:850-855, before MISC write */
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MISC_WRITE, misc));
    for (u32 i = 0; i < COUNT(mode_sr); ++i)
        TRY(vga_write(d, TRI_SR_INDEX, i + 1, mode_sr[i]));
    TRY(vga_write(d, TRI_CR_COLOR, 0x11, cr[0x11] & 0x7F));
    for (u32 i = 0; i < COUNT(cr); ++i)
        TRY(vga_write(d, TRI_CR_COLOR, i, cr[i]));
    for (u32 i = 0; i < COUNT(mode_gr); ++i)
        TRY(vga_write(d, TRI_GR_INDEX, i, mode_gr[i]));
    TRY(palette_access(d, true));
    for (u32 i = 0; i < COUNT(mode_ar); ++i)
        TRY(trident_ar_write(d, TRI_CR_COLOR, i, mode_ar[i]));
    TRY(palette_access(d, false));

    /* TridentRestore: all 9682 writes in reference order and width. */
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0B, &ignored));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0E, 0xC2));
    TRY(trident_hidden_dac_write(d, dac));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x1E, 0x80));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x21, 0x20));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0D, newmode2));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x50, 0));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x27, hiord));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x2B, hover));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x29, (addcol & 0xCF) | ((offset & 0x300) >> 4)));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x36, no_accel ? 0 : 0x80));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x2F, performance | 0x10));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x2A, interface | 0x40));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x3A, 0));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x38, pixel_bus));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x39, (pci & 0xF9) | 0x06));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x55, 0x1F));
    TRY(trident_indexed_write(d, TRI_GR_INDEX, 0x2F, misc_int | 0x04));
    TRY(trident_indexed_write(d, TRI_GR_INDEX, 0x0F, (misc_ext & 0xF0) | ext));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x13, offset & 0xFF));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_PLL_LO, pll[0]));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_PLL_HI, pll[1]));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MISC_WRITE, (misc & 0xF3) | MODE_CLOCK_SELECT));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0E, 0xC0 ^ 0x02));
    TRY(trident_protect(d, TRI_CR_COLOR, false));
    int format = mode_format(&m);
    d->mode.selected = m;
    d->mode.mux = mux;
    d->mode.no_accel = no_accel;
    d->mode.features = tri_format_caps[format].features;
    if (no_accel) d->mode.features &= ~TRI_CAP_GE;
    if (d->settings.sw_cursor || (mux && m.bpp == 8))
        d->mode.features &= ~TRI_CAP_CURSOR;
    d->mode.valid = true;
    return TRI_OK;
}

/* Compatibility entry: default timing/format, with current settings/pitch.
 * F9/F10 use tri_mode_set for the selected mode on subsequent entry. */
int trident_mode_640(struct trident_device *d)
{
    return tri_mode_set(d, &tri_modes[TRI_MODE_DEFAULT_ID - 1u]);
}
