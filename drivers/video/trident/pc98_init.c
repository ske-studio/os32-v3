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
/* PC98TRIDENT96xxInit: MIT trident_driver-1.4.0.c:616-692.
 * Script: userland/tests/hw/trident_mode.hwb:521-577; add the reference's
 * entry/exit protect (D:622,690). Sync source: NEC LCD172VXM/192VXM manual
 * p.20; the 9682 register manual is unavailable (H:50-52).
 */
#include "trident.h"

/* BAR1 offsets, NOT native I/O ports: D:636-647, trident_regs.h:302-325. */
enum {
    TRI_CONFIG_SETUP = 0x94,
    TRI_CONFIG_ENABLE = 0x102,
    TRI_VIDEO_ENABLE = 0x3C3,
    TRI_CONFIG_ALT = 0x46E8
};

#define TRY(operation) do { int rc = (operation); if (rc) return rc; } while (0)

int trident_pc98_init(struct trident_device *d)
{
    u8 temp, config;
    /* vgaHW keeps the IOBase selected at Save, even after MISC |= C3. */
    TRY(trident_protect(d, d->saved.vga_base, true));

    /* D:624-632 / H:522-529: subsystem enable, old mode, configuration 1. */
    TRY(trident_read8(d, TRI_SPACE_REGS, TRI_MISC_READ, &temp));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MISC_WRITE, temp | 0xC3));
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0B, &temp));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0B, temp));
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0E, &temp));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0E, temp | 0x20));

    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0C, &config));
    if (config & 0x10) {
        /* D:637 (design D:636) is OUTB, discarding temp's upper byte. */
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX, (u8)(0x0E | (temp << 8))));
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_CONFIG_SETUP, 0x00));
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_CONFIG_ENABLE, 0x01));
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_CONFIG_SETUP, 0x20));
        TRY(trident_read8(d, TRI_SPACE_REGS, TRI_VIDEO_ENABLE, &temp));
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_VIDEO_ENABLE, temp | 0x01));
    } else {
        /* D:644 (design D:643) is OUTB; H:543-546 retains this alternate path. */
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX, (u8)(0x0E | (temp << 8))));
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_CONFIG_ALT, 0x10));
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_CONFIG_ENABLE, 0x01));
        TRY(trident_write8(d, TRI_SPACE_REGS, TRI_CONFIG_ALT, 0x08));
    }

    /* D:649-658 / H:548-559. explicit AR access stays color; vgaHW uses Save IOBase. */
    TRY(trident_ar_write(d, TRI_CR_COLOR, 0x10, 0x41));
    TRY(trident_crtc_lock(d, d->saved.vga_base, false));
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0B, &temp));
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0E, &temp));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0E, temp | 0x80));

    /* D:661-670 / H:560-565: both FIFO writes, all latency and clock tuning. */
    TRY(trident_write16(d, TRI_SPACE_REGS, TRI_CR_COLOR, 0x3820));
    TRY(trident_write16(d, TRI_SPACE_REGS, TRI_CR_COLOR, 0x2020));
    TRY(trident_write16(d, TRI_SPACE_REGS, TRI_CR_COLOR, 0x0E30));
    TRY(trident_write16(d, TRI_SPACE_REGS, TRI_CR_COLOR, 0x0031));
    TRY(trident_write16(d, TRI_SPACE_REGS, TRI_CR_COLOR, 0x0032));
    TRY(trident_write16(d, TRI_SPACE_REGS, TRI_CR_COLOR, 0x213B));

    /* D:673 / H:566-567: active 80MHz setting, not the disabled examples. */
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MCLK_LO, 0xAF));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_MCLK_HI, 0x00));

    /* D:685-690 / H:568-577. Lifecycle owns init_done, not this primitive. */
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0B, &temp));
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0E, &temp));
    TRY(trident_indexed_write(d, TRI_SR_INDEX, 0x0E, temp & 0x7F));
    TRY(trident_crtc_lock(d, d->saved.vga_base, true));
    TRY(trident_protect(d, d->saved.vga_base, false));
    return TRI_OK;
}
#undef TRY
