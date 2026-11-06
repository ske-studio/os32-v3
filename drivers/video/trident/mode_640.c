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
/* Fixed 640x480x8, 25.175MHz, non-Cyber/old PLL, PCI MMIO (NoAccel register image).
 * Sources (MIT): trident_driver-1.4.0.c:940-1047;
 * trident_dac.c:235-278,572-576,631-749,781-810,871-907;
 * trident_pll.c:42-135; vgaHW.c:369-409,845-872,1219-1353,1391-1468.
 * Manufacturer sync reference: NEC LCD172VXM/192VXM manual p.20;
 * 9682 datasheet unavailable. H = userland/tests/hw/trident_mode.hwb.
 *
 * Differences from H:148-320, following the reference (PACK priority):
 * - Unlock precedes TridentInit; read mode inputs before Protect/Restore,
 *   including CR3A selection and SR20/SR12 reads, even when unused on 9682.
 * - Unlock/TridentInit/entry Protect use the current base; VGA Restore selects
 *   color, which subsequent TridentRestore/exit Protect also use (H: 3D4).
 * - VGA SR/CR/GR writes are two OUTBs, SR01=01 and initial CR13=28;
 *   TridentRestore later writes CR13=50. H uses OUTWs, 21 and 50.
 * - VGA Restore brackets AR with enable/disablePalette (H omits these).
 * - DRAMControl CR3A stays zero on 9682 (dac.c:673-678); GraphEngReg
 *   CR36=00 with NoAccel (X/trident_dac.c:694-700).
 * - PACK item 2 differs from D:2504-2529 and pll.c:42-55,71: 9682
 *   leaves frequency unset, so IsClearTV reads CR C0 on first ModeInit only
 *   and caches PAL/NTSC; H:150-156 reads it on every mode change.
 * Enable is split out to lifecycle, per the frozen contract (D:1043-1044).
 * PC-98 skips WAITFORVSYNC (D:948); no PLL wait or invented retry limit.
 */
#include "trident.h"

/* vgaHWInit + KGA_ENABLE_ON_ZERO: 800/640/656/752 horizontal,
 * 525/480/490/492 vertical. The extended Offset is written separately. */
static const u8 mode_sr[] = { 0x01, 0x0F, 0x00, 0x0E }; /* SR1..4 */
static const u8 mode_cr[] = {
    0x5F, 0x4F, 0x4F, 0x80, 0x52, 0x1E, 0x0B, 0x3E,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEA, 0x2C, 0xDF, 0x28, 0x00, 0xDF, 0x0B, 0xC3, 0xFF
};
static const u8 mode_gr[] = { 0, 0, 0, 0, 0, 0x40, 0x05, 0x0F, 0xFF };
static const u8 mode_ar[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    0x41, 0, 0x0F, 0, 0
};
/* TGUISetClock old-code search for 25175kHz (n=0..121, m=1..31,
 * k=0..1, strict improving error <750kHz), regs.h:27-28 references.
 * NTSC 14.31818MHz: n=101,m=29,k=1; PAL 17.73448: n=36,m=29,k=0.
 * Fixed mode needs only these two results; no floating-point arithmetic. */
static const u8 mode_pll[2][2] = { { 0xE5, 0x1E }, { 0xA4, 0x0E } };
#define MODE_MISC 0xE3u
#define MODE_CLOCK_MISC 0xEBu
#define MODE_OFFSET (TRI_PITCH >> 3)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define TRY(expr) do { int rc = (expr); if (rc) return rc; } while (0)

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

int trident_mode_640(struct trident_device *d)
{
    u8 ignored, misc_ext, interface, performance, addcol, misc_int, pci;
    if (!d) return TRI_E_INVAL;
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
    const u8 *pll = mode_pll[(d->reference & 0x80) != 0];
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
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MISC_WRITE, MODE_MISC));
    for (u32 i = 0; i < COUNT(mode_sr); ++i)
        TRY(vga_write(d, TRI_SR_INDEX, i + 1, mode_sr[i]));
    TRY(vga_write(d, TRI_CR_COLOR, 0x11, mode_cr[0x11] & 0x7F));
    for (u32 i = 0; i < COUNT(mode_cr); ++i)
        TRY(vga_write(d, TRI_CR_COLOR, i, mode_cr[i]));
    for (u32 i = 0; i < COUNT(mode_gr); ++i)
        TRY(vga_write(d, TRI_GR_INDEX, i, mode_gr[i]));
    TRY(palette_access(d, true));
    for (u32 i = 0; i < COUNT(mode_ar); ++i)
        TRY(trident_ar_write(d, TRI_CR_COLOR, i, mode_ar[i]));
    TRY(palette_access(d, false));

    /* TridentRestore: all 9682 writes in reference order and width. */
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0B, &ignored));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0E, 0xC2));
    TRY(trident_hidden_dac_write(d, 0));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x1E, 0x80));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x21, 0x20));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0D, 0x20));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x50, 0));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x27, 0x08));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x2B, 0));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x29, addcol & 0xCF));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x36, 0));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x2F, performance | 0x10));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x2A, interface | 0x40));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x3A, 0));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x38, 0));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x39, (pci & 0xF9) | 0x06));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x55, 0x1F));
    TRY(trident_indexed_write(d, TRI_GR_INDEX, 0x2F, misc_int | 0x04));
    TRY(trident_indexed_write(d, TRI_GR_INDEX, 0x0F, (misc_ext & 0xF0) | 0x02));
    TRY(trident_indexed_write(d, TRI_CR_COLOR, 0x13, MODE_OFFSET));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_PLL_LO, pll[0]));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_PLL_HI, pll[1]));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MISC_WRITE, MODE_CLOCK_MISC));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0E, 0xC0 ^ 0x02));
    return trident_protect(d, TRI_CR_COLOR, false);
}
