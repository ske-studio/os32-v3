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
/* Widths: MIT trident_regs.h:302-325. Sequences: vgaHW.c:574-604,
 * 1795-1806; trident_dac.c:781-791,1009-1016; trident_mode.hwb:493-518.
 * Resource pointers are already mapped by the kernel; no physical casts here.
 */
#include "trident.h"
#include "io.h"

/* Validate the complete access before forming an MMIO pointer. */
static int range(const struct trident_resources *r, enum trident_space space,
                 u32 off, u32 width)
{
    if (!r) return TRI_E_INVAL;
    u32 limit;
    if (space == TRI_SPACE_REGS && r->regs) limit = r->regs_bytes;
    else if (space == TRI_SPACE_LFB && r->lfb) {
        limit = r->mapped_bytes;
        if (limit > r->decode_bytes) limit = r->decode_bytes;
    } else return TRI_E_ACCESS;
    if ((off & (width - 1)) || off > limit || width > limit - off)
        return TRI_E_ACCESS;
    return 0;
}
static volatile u8 *base(const struct trident_resources *r,
                          enum trident_space space)
{
    return space == TRI_SPACE_REGS ? r->regs : r->lfb;
}
#define MMIO_ACCESS(bits, bytes) \
static int mmio_read##bits(void *opaque, enum trident_space s, u32 off, u##bits *v) \
{ \
    const struct trident_resources *r = opaque; \
    if (!v) return TRI_E_INVAL; \
    int rc = range(r, s, off, bytes); \
    if (rc) return rc; \
    *v = *(volatile u##bits *)(base(r, s) + off); \
    return 0; \
} \
static int mmio_write##bits(void *opaque, enum trident_space s, u32 off, u##bits v) \
{ \
    const struct trident_resources *r = opaque; \
    int rc = range(r, s, off, bytes); \
    if (rc) return rc; \
    *(volatile u##bits *)(base(r, s) + off) = v; \
    return 0; \
}
MMIO_ACCESS(8, 1)
MMIO_ACCESS(16, 2)
MMIO_ACCESS(32, 4)

static int port8(void *opaque, u16 port, u8 value)
{
    (void)opaque;
    outp(port, value);
    return 0;
}
static int write_span32(void *opaque, u32 off, const u32 *src, u32 count)
{
    const struct trident_resources *r = opaque;
    if (!src || !count || ((uptr)src & 3)) return TRI_E_ACCESS;
    int rc = range(r, TRI_SPACE_LFB, off, 4);
    if (rc) return rc;
    u32 limit = r->mapped_bytes;
    if (limit > r->decode_bytes) limit = r->decode_bytes;
    if (count > (limit - off) / 4) return TRI_E_ACCESS;
    volatile u32 *dst = (volatile u32 *)(r->lfb + off);
    for (u32 i = 0; i < count; ++i) dst[i] = src[i];
    return 0;
}
static const struct trident_io mmio_io = {
    .read8 = mmio_read8, .read16 = mmio_read16, .read32 = mmio_read32,
    .write8 = mmio_write8, .write16 = mmio_write16, .write32 = mmio_write32,
    .port8 = port8, .write_span32 = write_span32
};
int trident_regs_bind(struct trident_device *d)
{
    if (!d || !d->res) return TRI_E_INVAL;
    d->io = &mmio_io;
    d->io_opaque = (void *)d->res;
    return 0;
}
#define WRAPPERS(bits) \
int trident_read##bits(struct trident_device *d, enum trident_space s, u32 off, u##bits *v) \
{ \
    if (!d || !d->io || !d->io->read##bits || !v) return TRI_E_INVAL; \
    return d->io->read##bits(d->io_opaque, s, off, v); \
} \
int trident_write##bits(struct trident_device *d, enum trident_space s, u32 off, u##bits v) \
{ \
    if (!d || !d->io || !d->io->write##bits) return TRI_E_INVAL; \
    return d->io->write##bits(d->io_opaque, s, off, v); \
}
WRAPPERS(8)
WRAPPERS(16)
WRAPPERS(32)
int trident_port8(struct trident_device *d, u16 port, u8 value)
{
    if (!d || !d->io || !d->io->port8) return TRI_E_INVAL;
    return d->io->port8(d->io_opaque, port, value);
}
#define TRY(op) do { int rc_ = (op); if (rc_) return rc_; } while (0)
#define READ(off, v) trident_read8(d, TRI_SPACE_REGS, (off), (v))
#define WRITE(off, v) trident_write8(d, TRI_SPACE_REGS, (off), (v))
int trident_indexed_read(struct trident_device *d, u16 port, u8 index, u8 *v)
{
    if (!v) return TRI_E_INVAL;
    TRY(WRITE(port, index));
    return READ(port + 1u, v);
}
int trident_indexed_write(struct trident_device *d, u16 port, u8 index, u8 v)
{
    return trident_write16(d, TRI_SPACE_REGS, port, (u16)(index | ((u16)v << 8)));
}
int trident_ar_read(struct trident_device *d, u16 vga_base, u8 index, u8 *v)
{
    u8 discard;
    if (!v) return TRI_E_INVAL;
    TRY(READ(vga_base + 6u, &discard));
    TRY(WRITE(TRI_AR_INDEX, index));
    return READ(TRI_AR_DATA, v);
}
int trident_ar_write(struct trident_device *d, u16 vga_base, u8 index, u8 v)
{
    u8 discard;
    TRY(READ(vga_base + 6u, &discard));
    TRY(WRITE(TRI_AR_INDEX, index));
    return WRITE(TRI_AR_INDEX, v);
}
static int hidden_select(struct trident_device *d)
{
    u8 discard;
    TRY(READ(TRI_DAC_WRITE_INDEX, &discard));
    for (u32 i = 0; i < 4; ++i) TRY(READ(TRI_DAC_MASK, &discard));
    return 0;
}
int trident_hidden_dac_read(struct trident_device *d, u8 *v)
{
    u8 value, discard;
    if (!v) return TRI_E_INVAL;
    TRY(hidden_select(d));
    TRY(READ(TRI_DAC_MASK, &value));
    TRY(READ(TRI_DAC_WRITE_INDEX, &discard));
    *v = value;
    return 0;
}
int trident_hidden_dac_write(struct trident_device *d, u8 v)
{
    u8 discard;
    TRY(hidden_select(d));
    TRY(WRITE(TRI_DAC_MASK, v));
    return READ(TRI_DAC_WRITE_INDEX, &discard);
}
int trident_ext_dac_update(struct trident_device *d, u8 mask, u8 bits)
{
    u8 v;
    TRY(WRITE(TRI_EXT_DAC_INDEX, 4));
    TRY(READ(TRI_EXT_DAC_DATA, &v));
    TRY(WRITE(TRI_EXT_DAC_INDEX, 4));
    return WRITE(TRI_EXT_DAC_DATA, (v & mask) | bits);
}
/* vgaHW writeSeq/writeCrtc are two OUTBs, unlike Trident OUTW. */
static int vga_write(struct trident_device *d, u16 port, u8 index, u8 v)
{
    TRY(WRITE(port, index));
    return WRITE(port + 1u, v);
}
int trident_protect(struct trident_device *d, u16 vga_base, bool on)
{
    u8 seq, discard;
    if (!d) return TRI_E_INVAL;
    if (!d->active) return 0; /* V/vgaHW.c:580 vtSema */
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 1, &seq));
    if (on) {
        TRY(vga_write(d, TRI_SR_INDEX, 0, 1));
        TRY(vga_write(d, TRI_SR_INDEX, 1, seq | 0x20));
    } else {
        TRY(vga_write(d, TRI_SR_INDEX, 1, seq & 0xDF));
        TRY(vga_write(d, TRI_SR_INDEX, 0, 3));
    }
    TRY(READ(vga_base + 6u, &discard));
    return WRITE(TRI_AR_INDEX, on ? 0 : 0x20);
}
int trident_crtc_lock(struct trident_device *d, u16 vga_base, bool lock)
{
    u8 v;
    TRY(trident_indexed_read(d, vga_base, 0x11, &v));
    return vga_write(d, vga_base, 0x11, lock ? (v | 0x80) : (v & 0x7F));
}
