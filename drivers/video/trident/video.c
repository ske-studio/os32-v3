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
/* X/trident_video.c:225-270,350-383,1332-1343;
 * X/trident_regs.h:254-258,343-351. 9682, depth 8, default video key.
 * X/trident_driver.c:1647-1650: depth-8 masks/offsets are zero, key = -1.
 * D:3744-3748 calls InitVideo at the end of each ScreenInit.
 */
#include "trident.h"
#define TRY(call) do { int rc = (call); if (rc) return rc; } while (0)
static int video_out(struct trident_device *d, u8 index, u8 value)
{
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_EXT_DAC_INDEX, index));
    return trident_write8(d, TRI_SPACE_REGS, TRI_EXT_DAC_DATA, value);
}
int trident_video_reset(struct trident_device *d)
{
    u8 status;
    if (!d) return TRI_E_INVAL;
    /* WaitForVBlank: two complete low->high transitions, without a timeout. */
    for (u32 frame = 0; frame < 2; ++frame) {
        do { TRY(trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6u, &status)); }
        while (status & 8);
        do { TRY(trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6u, &status)); }
        while (!(status & 8));
    }
    TRY(trident_indexed_write(d, d->cur_vga_base, 0x8E, 0x84));
    TRY(video_out(d, 0x57, 0xC0));
    TRY(trident_indexed_write(d, d->cur_vga_base, 0xBE, 0x26));
    TRY(video_out(d, 0x50, 0xFF));
    TRY(video_out(d, 0x51, 0));
    TRY(video_out(d, 0x52, 0));
    TRY(video_out(d, 0x54, 0xFF));
    TRY(video_out(d, 0x55, 0));
    return video_out(d, 0x56, 0);
}
