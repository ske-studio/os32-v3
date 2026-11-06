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
/* Restore unit adapter: regs.c is a separate 3b deliverable. These helpers
 * implement its frozen interface over the SHARED device fake, not another
 * device model. Production bind/lifecycle assembly is checked in 3c/3d.
 * Access sequences: vgaHW.c:369-409,438-448,474-488,574-604;
 * trident_mode.hwb:499-519,1503-1509. Source notices: state_restore.c.
 */
#include "tools/tests/trident_device_fake.h"
#define TRY(op) do { int rc = (op); if (rc) return rc; } while (0)
int trident_read8(struct trident_device *d, enum trident_space s, u32 o, u8 *v)
{ return d->io->read8(d->io_opaque, s, o, v); }
int trident_write8(struct trident_device *d, enum trident_space s, u32 o, u8 v)
{ return d->io->write8(d->io_opaque, s, o, v); }
int trident_indexed_read(struct trident_device *d, u16 p, u8 i, u8 *v)
{
    TRY(trident_write8(d, TRI_SPACE_REGS, p, i));
    return trident_read8(d, TRI_SPACE_REGS, p + 1u, v);
}
int trident_indexed_write(struct trident_device *d, u16 p, u8 i, u8 v)
{ return d->io->write16(d->io_opaque, TRI_SPACE_REGS, p, (u16)(i | (v << 8))); }
static int pair(struct trident_device *d, u16 p, u8 i, u8 v)
{
    TRY(trident_write8(d, TRI_SPACE_REGS, p, i));
    return trident_write8(d, TRI_SPACE_REGS, p + 1u, v);
}
int trident_ar_write(struct trident_device *d, u16 base, u8 i, u8 v)
{
    u8 ignored;
    TRY(trident_read8(d, TRI_SPACE_REGS, base + 6u, &ignored));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, i));
    return trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, v);
}
int trident_hidden_dac_write(struct trident_device *d, u8 v)
{
    u8 ignored;
    TRY(trident_read8(d, TRI_SPACE_REGS, TRI_DAC_WRITE_INDEX, &ignored));
    for (u32 i = 0; i < 4; ++i)
        TRY(trident_read8(d, TRI_SPACE_REGS, TRI_DAC_MASK, &ignored));
    TRY(trident_write8(d, TRI_SPACE_REGS, TRI_DAC_MASK, v));
    return trident_read8(d, TRI_SPACE_REGS, TRI_DAC_WRITE_INDEX, &ignored);
}
int trident_protect(struct trident_device *d, u16 base, bool on)
{
    u8 v, ignored;
    TRY(trident_indexed_read(d, TRI_SR_INDEX, 1, &v));
    if (on) TRY(pair(d, TRI_SR_INDEX, 0, 1));
    TRY(pair(d, TRI_SR_INDEX, 1, on ? v | 0x20 : v & 0xDF));
    if (!on) TRY(pair(d, TRI_SR_INDEX, 0, 3));
    TRY(trident_read8(d, TRI_SPACE_REGS, base + 6u, &ignored));
    return trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, on ? 0 : 0x20);
}

static struct trident_fake fake, seed;
static struct trident_fake_event trace[4096]; /* 768 RGB * 3 + mode/protect */
static u8 storage[TRI_FAKE_REAL_BYTES];
static struct trident_device device, initial;
static void copy(void *dst, const void *src, u32 bytes)
{
    u8 *d = dst;
    const u8 *s = src;
    for (u32 i = 0; i < bytes; ++i) d[i] = s[i];
}
static int equal(u32 a, u32 b)
{
    __asm__ volatile ("" : "+r"(a), "+r"(b));
    return a == b;
}
#define REQUIRE(c) do { if (!(c)) return __LINE__; } while (0)
static int emit(const void *p, u32 n)
{
    int rc;
    __asm__ volatile ("int $0x80" : "=a"(rc) : "a"(4), "b"(1), "c"(p), "d"(n) : "memory");
    return rc == (int)n ? 0 : 1;
}
static int scenario(u32 current, u32 saved)
{
    REQUIRE(!trident_fake_reset(&fake, storage, sizeof(storage), trace,
                               sizeof(trace) / sizeof(trace[0])));
    device = (struct trident_device){0};
    trident_fake_bind(&fake, &device);
    REQUIRE(!device.active && !device.saved.valid && !device.pinned && !device.armed);
    REQUIRE(!trident_state_restore(&device) && !fake.operations);
    device.active = true;
    REQUIRE(trident_state_restore(&device) == TRI_E_STATE && !fake.operations);
    REQUIRE(trident_state_restore(0) == TRI_E_INVAL);
    device.saved.valid = true;
    device.saved.vga_base = saved ? TRI_CR_COLOR : TRI_CR_MONO;
    /* Distinct synthetic saved values, not observed boot values. DAC units
     * are six bits; no HAL conversion is appropriate during restoration. */
    for (u32 i = 0; i < TRI_SAVED_BYTES; ++i)
        device.saved.value[i] = (u8)((i * 37u + 11u) & (i < 768 ? 63u : 255u));
    device.saved.value[768] = (u8)(0xA0 | saved);
    fake.regs[TRI_MISC_WRITE] = (u8)(0xC2 | current);
    fake.sr[1] = 0x15;
    device.pinned = device.armed = true;
    device.last_rc = -91;
    copy(&seed, &fake, sizeof(seed));
    initial = device;
    REQUIRE(!trident_state_restore(&device));
    REQUIRE(equal(device.cur_vga_base, saved ? TRI_CR_COLOR : TRI_CR_MONO));
    REQUIRE(!fake.trace_overflow);
    u32 count = fake.trace_count;
    REQUIRE(count == fake.operations);
    REQUIRE(!emit(&count, sizeof(count)));
    REQUIRE(!emit(trace, count * sizeof(trace[0])));
    REQUIRE(equal(fake.sr[0], 3));
    REQUIRE(equal(fake.sr[1], initial.saved.value[824] & 0xDF));
    REQUIRE(equal(fake.sr[0x0E], initial.saved.value[828] ^ 2));
    REQUIRE(equal(fake.hidden_dac, initial.saved.value[845]));
    REQUIRE(equal(fake.regs[TRI_DAC_MASK], 255));
    REQUIRE(equal(fake.gr[6], initial.saved.value[821]));
    for (u32 i = 0; i < TRI_DAC_BYTES; ++i)
        REQUIRE(equal(fake.dac[i], initial.saved.value[i]));
    for (u32 i = 0; i < 21; ++i)
        REQUIRE(equal(fake.ar[i], initial.saved.value[794 + i]));
    REQUIRE(device.active && device.saved.valid && device.last_rc == -91);
    for (u32 i = 0; i < TRI_SAVED_BYTES; ++i)
        REQUIRE(equal(device.saved.value[i], initial.saved.value[i]));
    /* Every access, including helper internals and final unprotect, must
     * preserve the injected rc and stop at the failed operation. */
    for (u32 n = 1; n <= count; ++n) {
        copy(&fake, &seed, sizeof(fake));
        device = initial;
        trident_fake_fail(&fake, n, -123);
        REQUIRE(trident_state_restore(&device) == -123);
        REQUIRE(equal(fake.operations, n) && equal(fake.trace_count, n));
        REQUIRE(trace[n - 1].rc == -123);
        REQUIRE(device.active && device.saved.valid && device.last_rc == -91);
    }
    return 0;
}
static int tests(void)
{
    for (u32 c = 0; c < 2; ++c)
        for (u32 s = 0; s < 2; ++s) {
            int rc = scenario(c, s);
            if (rc) return rc;
        }
    return 0;
}
void _start(void)
{
    int rc = tests();
    /* Preserve an error even when its source line is divisible by 256. */
    if (rc) rc = rc % 254 + 1;
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
