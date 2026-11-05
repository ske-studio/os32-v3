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
/* TRIDENTSave (driver-1.4.0.c:1084-1104), vgaHW.c:1016-1166,
 * trident_dac.c:912-1048; script trident_mode.hwb:587-1496.
 * 9682, programmable old PLL, non-Cyber, nonprimary: no font/text planes.
 */
#include "trident.h"
#include "drivers/char/serial.h"

#define SAVE_TRY(operation) do { int rc = (operation); if (rc) return rc; } while (0)

static int read_reg(struct trident_device *d, u32 port, u8 *value)
{
    return trident_read8(d, TRI_SPACE_REGS, port, value);
}

static int write_reg(struct trident_device *d, u32 port, u8 value)
{
    return trident_write8(d, TRI_SPACE_REGS, port, value);
}

static int palette_access(struct trident_device *d, u16 base, u8 enable)
{
    u8 ignored;
    SAVE_TRY(read_reg(d, base + 6, &ignored));
    return write_reg(d, TRI_AR_INDEX, enable);
}

static int save_palette(struct trident_device *d, u16 base)
{
    u8 *save = d->saved.value;
    u8 value;
    bool badmap = false;

    /* vgaHWSaveColormap: test foreground RGB, undo the test, then save.
     * A value mismatch is diagnostic only, never the display's rc. */
    SAVE_TRY(write_reg(d, TRI_DAC_MASK, 0xFF));
    SAVE_TRY(write_reg(d, TRI_DAC_READ_INDEX, 0));
    for (u32 i = 0; i < 6; ++i)
        SAVE_TRY(read_reg(d, TRI_DAC_DATA, &save[i]));
    SAVE_TRY(write_reg(d, TRI_DAC_WRITE_INDEX, 1));
    for (u32 i = 3; i < 6; ++i)
        SAVE_TRY(write_reg(d, TRI_DAC_DATA, (save[i] ^ 0x3F) & 0x3F));
    SAVE_TRY(write_reg(d, TRI_DAC_READ_INDEX, 1));
    for (u32 i = 3; i < 6; ++i) {
        SAVE_TRY(read_reg(d, TRI_DAC_DATA, &value));
        if (value != ((save[i] ^ 0x3F) & 0x3F))
            badmap = true;
    }
    SAVE_TRY(write_reg(d, TRI_DAC_WRITE_INDEX, 1));
    for (u32 i = 3; i < 6; ++i)
        SAVE_TRY(write_reg(d, TRI_DAC_DATA, save[i]));
    if (badmap) {
        serial_puts("[trident] stage=save-palette rc=-6 default DAC\n");
        for (u32 i = 0; i < TRI_DAC_BYTES; ++i)
            save[i] = trident_default_dac[i];
    } else {
        SAVE_TRY(write_reg(d, TRI_DAC_READ_INDEX, 2));
        for (u32 i = 6; i < TRI_DAC_BYTES; ++i) {
            SAVE_TRY(read_reg(d, TRI_DAC_DATA, &save[i]));
            /* DACDelay: two input-status reads, not a timed poll. */
            SAVE_TRY(read_reg(d, base + 6, &value));
            SAVE_TRY(read_reg(d, base + 6, &value));
        }
    }
    return palette_access(d, base, 0x20);
}

static int save_vga(struct trident_device *d)
{
    u8 *save = d->saved.value;
    SAVE_TRY(read_reg(d, TRI_MISC_READ, &save[TRI_SAVE_MISC]));
    u16 base = (save[TRI_SAVE_MISC] & 1) ? TRI_CR_COLOR : TRI_CR_MONO;
    d->saved.vga_base = base;
    for (u32 i = 0; i < 25; ++i)
        SAVE_TRY(trident_indexed_read(d, base, i, &save[TRI_SAVE_CR + i]));
    SAVE_TRY(palette_access(d, base, 0));
    for (u32 i = 0; i < 21; ++i)
        SAVE_TRY(trident_ar_read(d, base, i, &save[TRI_SAVE_AR + i]));
    SAVE_TRY(palette_access(d, base, 0x20));
    for (u32 i = 0; i < 9; ++i)
        SAVE_TRY(trident_indexed_read(d, TRI_GR_INDEX, i, &save[TRI_SAVE_GR + i]));
    for (u32 i = 1; i < 5; ++i)
        SAVE_TRY(trident_indexed_read(d, TRI_SR_INDEX, i, &save[TRI_SAVE_SR + i - 1]));
    return 0;
}

static int save_trident(struct trident_device *d)
{
    /* trident_dac.c:935-947,1003; order matches H:1447-1474. */
    static const u8 cr[] = {
        0x13, 0x21, 0x1E, 0x27, 0x2B, 0x2F, 0x2A,
        0x3A, 0x29, 0x38, 0x36, 0x39, 0x55, 0x50
    };
    u8 *save = d->saved.value;
    u8 ignored;
    SAVE_TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0B, &ignored));
    SAVE_TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0E, &save[TRI_SAVE_SR0E]));
    SAVE_TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0E, 0xC2));
    for (u32 i = 0; i < sizeof(cr); ++i)
        SAVE_TRY(trident_indexed_read(d, d->saved.vga_base, cr[i],
                                      &save[TRI_SAVE_CR13 + i]));
    SAVE_TRY(trident_indexed_read(d, TRI_GR_INDEX, 0x0F, &save[TRI_SAVE_GR0F]));
    SAVE_TRY(trident_indexed_read(d, TRI_GR_INDEX, 0x2F, &save[TRI_SAVE_GR2F]));
    SAVE_TRY(trident_hidden_dac_read(d, &save[TRI_SAVE_HIDDEN_DAC]));
    SAVE_TRY(read_reg(d, TRI_MISC_READ, &save[TRI_SAVE_CLOCK_MISC]));
    SAVE_TRY(read_reg(d, TRI_PLL_LO, &save[TRI_SAVE_PLL_LO]));
    SAVE_TRY(read_reg(d, TRI_PLL_HI, &save[TRI_SAVE_PLL_HI]));
    if (TRI_SAVE_MCLK) {
        SAVE_TRY(read_reg(d, TRI_MCLK_LO, &save[TRI_SAVE_MCLK_LO]));
        SAVE_TRY(read_reg(d, TRI_MCLK_HI, &save[TRI_SAVE_MCLK_HI]));
    }
    SAVE_TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0D, &save[TRI_SAVE_SR0D]));
    return trident_indexed_write(d, TRI_SR_INDEX, 0x0E, save[TRI_SAVE_SR0E]);
}

int trident_state_save(struct trident_device *d)
{
    if (!d) return TRI_E_INVAL;
    if (d->saved.valid) return 0; /* Boot state lives for the module lifetime. */
    u8 misc;
    SAVE_TRY(read_reg(d, TRI_MISC_READ, &misc));
    u16 base = (misc & 1) ? TRI_CR_COLOR : TRI_CR_MONO;
    SAVE_TRY(save_palette(d, base));
    SAVE_TRY(save_vga(d));
    SAVE_TRY(save_trident(d));
    d->saved.valid = true;
    return 0;
}
