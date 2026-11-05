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
/* PC-98 Enable/Disable: MIT X.Org trident_driver-1.4.0.c:694-775.
 * NEC LCD172VXM/192VXM manual p.20 is the sync reference; the 9682
 * register manual is unavailable (trident_mode.hwb:50-52).
 * vgaHWProtect uses the current IOBase (vgaHW.c:850-855,1021-1025),
 * which changes at VGA mode/state restore. CR23/29 stay explicitly at 3D4.
 * Lifecycle owns pin/arm, saved state and error diagnostics; these primitives
 * return the first failing rc unchanged. No waits, retries or readback.
 * Coordination of 0FAC with pc98_route.c belongs to stage 3c.
 */
#include "trident.h"

#define ROUTE_TRY(operation) do { \
    int route_rc = (operation); \
    if (route_rc) return route_rc; \
} while (0)

/* OUTB index, INB data, then one OUTW (never two OUTBs). */
static int route_update(struct trident_device *d, u16 port, u8 index,
                        u8 mask, u8 bits)
{
    u8 value;
    ROUTE_TRY(trident_indexed_read(d, port, index, &value));
    return trident_indexed_write(d, port, index, (value & mask) | bits);
}

int trident_route_enable(struct trident_device *d)
{
    if (!d) return TRI_E_INVAL;
    /* D:699-704: switch the PC-98 flip-flops before protecting VGA. */
    ROUTE_TRY(trident_port8(d, TRI_PORT_MODE, 0x0E));
    ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x07));
    ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x8F));
    ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x06));
    ROUTE_TRY(trident_protect(d, d->cur_vga_base, true));

    /* D:706-727: every extended DAC update selects index 4 twice. */
    ROUTE_TRY(route_update(d, TRI_CR_COLOR, 0x23, 0xDF, 0x00));
    ROUTE_TRY(route_update(d, TRI_CR_COLOR, 0x29, 0xFF, 0x04));
    ROUTE_TRY(trident_ext_dac_update(d, 0xFF, 0x06));
    ROUTE_TRY(trident_ext_dac_update(d, 0xFF, 0x08));
    ROUTE_TRY(route_update(d, TRI_GR_INDEX, 0x23, 0xFC, 0x00));
    ROUTE_TRY(trident_ext_dac_update(d, 0xFF, 0x01));
    ROUTE_TRY(route_update(d, TRI_SR_INDEX, 0x01, 0xEF, 0x00));
    ROUTE_TRY(trident_protect(d, d->cur_vga_base, false));
    return trident_port8(d, TRI_PORT_ROUTE, 0x02);
}

int trident_route_disable(struct trident_device *d)
{
    if (!d) return TRI_E_INVAL;
    /* D:738-767: route back before touching VGA. */
    ROUTE_TRY(trident_port8(d, TRI_PORT_ROUTE, 0x00));
    ROUTE_TRY(trident_protect(d, d->cur_vga_base, true));
    ROUTE_TRY(route_update(d, TRI_SR_INDEX, 0x01, 0xFF, 0x10));
    ROUTE_TRY(trident_ext_dac_update(d, 0xFE, 0x00));
    ROUTE_TRY(route_update(d, TRI_GR_INDEX, 0x23, 0xFC, 0x01));
    ROUTE_TRY(trident_ext_dac_update(d, 0xFD, 0x00));
    ROUTE_TRY(trident_ext_dac_update(d, 0xCF, 0x00));
    ROUTE_TRY(trident_ext_dac_update(d, 0xF7, 0x00));
    ROUTE_TRY(trident_ext_dac_update(d, 0xFB, 0x00));
    ROUTE_TRY(route_update(d, TRI_CR_COLOR, 0x29, 0xFB, 0x00));
    ROUTE_TRY(route_update(d, TRI_CR_COLOR, 0x23, 0xFF, 0x20));
    ROUTE_TRY(trident_protect(d, d->cur_vga_base, false));

    /* D:769-772: preserve the complete closing flip-flop sequence. */
    ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x07));
    ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x8E));
    ROUTE_TRY(trident_port8(d, TRI_PORT_FF, 0x06));
    return trident_port8(d, TRI_PORT_MODE, 0x0F);
}
