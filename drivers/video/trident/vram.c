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
/* SPR: MIT trident_driver-1.4.0.c:2919-2975 (9682 mask 0F).
 * Manufacturer sync source: NEC LCD172VXM/192VXM manual p.20; the 9682
 * register manual is unavailable (H:50-52). No waits or invented timeout.
 */
#include "trident.h"

int trident_capacity_plan(struct trident_device *d)
{
    u8 spr, ext;
    u32 kib;
    int rc;
    if (!d || !d->res || !d->io) return TRI_E_INVAL;
    if (!d->armed || !d->pinned) return TRI_E_STATE;
    /* Use the reference's current VGA IOBase; H:120 hardcodes color instead. */
    u16 base = d->cur_vga_base;
    rc = trident_indexed_read(d, base, 0x1F, &spr);
    if (rc) return rc;
    bool unknown = false;
    switch (spr & 0x0F) {
    case 0x01: kib = 512; break;
    case 0x02: kib = 6144; break;
    case 0x03: kib = 1024; break;
    case 0x04: kib = 8192; break;
    case 0x06: kib = 10240; break;
    case 0x07: kib = 2048; break;
    case 0x08: kib = 12288; break;
    case 0x0A: kib = 14336; break;
    case 0x0C: kib = 16384; break;
    case 0x0E:
        rc = trident_indexed_read(d, base, 0xC1, &ext);
        if (rc) return rc;
        switch (ext & 0x11) {
        case 0x00: kib = 20480; break;
        case 0x01: kib = 24576; break;
        case 0x10: kib = 28672; break;
        default:   kib = 32768; break; /* masked 11 */
        }
        break;
    case 0x0F: kib = 4096; break;
    default: kib = 1024; unknown = true; break;
    }
    d->spr = spr;
    d->declared_bytes = kib * 1024u;
    d->declared_unknown = unknown;
    return TRI_OK;
}
