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
/* D/trident_driver-1.4.0.c:1299-1340, unchanged four-mode register sequence. */
#include "trident.h"
#define TRY(call) do { int rc = (call); if (rc) return rc; } while (0)
int trident_dpms_set(struct trident_device *d, enum trident_dpms mode)
{
    u8 temp, pm, dpms;
    if (!d) return TRI_E_INVAL;
    if (!d->active) return 0;
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 0x0E, &temp));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX + 1, 0xC2));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_EXT_DAC_INDEX, 4));
    TRY(trident_read8(d, TRI_SPACE_REGS, TRI_EXT_DAC_DATA, &pm));
    TRY(trident_indexed_read(d, TRI_GR_INDEX, 0x23, &dpms));
    pm &= 0xFC;
    dpms &= 0xFC;
    switch (mode) {
    case TRI_DPMS_ON: pm |= 3; break;
    case TRI_DPMS_STANDBY: pm |= 2; dpms |= 1; break;
    case TRI_DPMS_SUSPEND: pm |= 2; dpms |= 2; break;
    case TRI_DPMS_OFF: dpms |= 3; break;
    }
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_GR_INDEX + 1, dpms));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_EXT_DAC_INDEX, 4));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_EXT_DAC_DATA, pm));
    return trident_indexed_write(d, TRI_SR_INDEX, 0x0E, temp);
}
