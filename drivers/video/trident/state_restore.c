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
/* 9682 / nonprimary restore. TRIDENTRestore: trident_driver-1.4.0.c:
 * 1111-1136; TridentRestore: trident_dac.c:763-910 (MIT).
 * VGA mode/colormap/protect: vgaHW.c:845-892,574-604.
 * Script: trident_mode.hwb:1497-1585. VGA indexed writes follow the
 * reference mmioWriteSeq/Crtc/Gr (8bit index + data), rather than the
 * script's packed 16bit VGA writes. Trident OUTW remains 16bit.
 * Retained IOBase selects entry; saved MISC selects VGA/exit base.
 * No fonts on this nonprimary card.
 */
#include "trident.h"

#define TRY(operation) do { int rc = (operation); if (rc) return rc; } while (0)

/* vgaHW.c mmioWriteCrtc/Gr/Seq, not Trident's OUTW macros. */
static int vga_write(struct trident_device *d, u16 port, u8 index, u8 value)
{
    TRY(trident_write8(d, TRI_SPACE_REGS, port, index));
    return trident_write8(d, TRI_SPACE_REGS, port + 1u, value);
}

static int palette_access(struct trident_device *d, u16 base, u8 value)
{
    u8 ignored;
    TRY(trident_read8(d, TRI_SPACE_REGS, base + 6u, &ignored));
    return trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, value);
}

int trident_state_restore(struct trident_device *d)
{
    u8 ignored;
    u16 base;
    if (!d) return TRI_E_INVAL;
    if (!d->active) return TRI_OK;
    if (!d->saved.valid) return TRI_E_STATE;
    const u8 *s = d->saved.value;

    base = d->cur_vga_base; /* D:1123 retains vgaHW IOBase. */
    TRY(trident_protect(d, base, true));

    /* TridentRestore, old PLL path; default MCLK is not saved. */
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0B, &ignored));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0E, 0xC2));
    TRY(trident_hidden_dac_write(d, s[TRI_SAVE_HIDDEN_DAC]));
    TRY(trident_indexed_write(d, base, 0x1E, s[TRI_SAVE_CR1E]));
    TRY(trident_indexed_write(d, base, 0x21, s[TRI_SAVE_CR21]));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0D, s[TRI_SAVE_SR0D]));
    TRY(trident_indexed_write(d, base, 0x50, s[TRI_SAVE_CR50]));
    TRY(trident_indexed_write(d, base, 0x27, s[TRI_SAVE_CR27]));
    TRY(trident_indexed_write(d, base, 0x2B, s[TRI_SAVE_CR2B]));
    TRY(trident_indexed_write(d, base, 0x29, s[TRI_SAVE_CR29]));
    TRY(trident_indexed_write(d, base, 0x36, s[TRI_SAVE_CR36]));
    TRY(trident_indexed_write(d, base, 0x2F, s[TRI_SAVE_CR2F]));
    TRY(trident_indexed_write(d, base, 0x2A, s[TRI_SAVE_CR2A]));
    TRY(trident_indexed_write(d, base, 0x3A, s[TRI_SAVE_CR3A]));
    TRY(trident_indexed_write(d, base, 0x38, s[TRI_SAVE_CR38]));
    TRY(trident_indexed_write(d, base, 0x39, s[TRI_SAVE_CR39]));
    TRY(trident_indexed_write(d, base, 0x55, s[TRI_SAVE_CR55]));
    TRY(trident_indexed_write(d, TRI_GR_INDEX, 0x2F, s[TRI_SAVE_GR2F]));
    TRY(trident_indexed_write(d, TRI_GR_INDEX, 0x0F, s[TRI_SAVE_GR0F]));
    TRY(trident_indexed_write(d, base, 0x13, s[TRI_SAVE_CR13]));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_PLL_LO, s[TRI_SAVE_PLL_LO]));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_PLL_HI, s[TRI_SAVE_PLL_HI]));
    if (TRI_SAVE_MCLK) {
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MCLK_LO, s[TRI_SAVE_MCLK_LO]));
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MCLK_HI, s[TRI_SAVE_MCLK_HI]));
    }
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MISC_WRITE, s[TRI_SAVE_CLOCK_MISC]));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0E, s[TRI_SAVE_SR0E] ^ 0x02));

    /* vgaHWRestoreMode: saved MISC switches the VGA IOBase. */
    base = (s[TRI_SAVE_MISC] & 1u) ? TRI_CR_COLOR : TRI_CR_MONO;
    d->cur_vga_base = base;
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MISC_WRITE, s[TRI_SAVE_MISC]));
    for (u8 i = 1; i < 5; ++i)
        TRY(vga_write(d, TRI_SR_INDEX, i, s[TRI_SAVE_SR + i - 1u]));
    TRY(vga_write(d, base, 0x11, s[TRI_SAVE_CR + 0x11] & 0x7F));
    for (u8 i = 0; i < 25; ++i)
        TRY(vga_write(d, base, i, s[TRI_SAVE_CR + i]));
    for (u8 i = 0; i < 9; ++i)
        TRY(vga_write(d, TRI_GR_INDEX, i, s[TRI_SAVE_GR + i]));
    TRY(palette_access(d, base, 0));
    for (u8 i = 0; i < 21; ++i)
        TRY(trident_ar_write(d, base, i, s[TRI_SAVE_AR + i]));
    TRY(palette_access(d, base, 0x20));

    /* vgaHWRestoreColormap: DACDelay is two status reads per component. */
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_DAC_MASK, 0xFF));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_DAC_WRITE_INDEX, 0));
    for (u32 i = 0; i < TRI_DAC_BYTES; ++i) {
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_DAC_DATA, s[TRI_SAVE_DAC + i]));
        TRY(trident_read8(d, TRI_SPACE_REGS, base + 6u, &ignored));
        TRY(trident_read8(d, TRI_SPACE_REGS, base + 6u, &ignored));
    }
    TRY(palette_access(d, base, 0x20));
    return trident_protect(d, base, false);
}
