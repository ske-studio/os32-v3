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
/* Route oracle: X.Org trident_driver-1.4.0.c:694-775 (MIT/X11),
 * vgaHW.c:369-409,574-604; notices retained above.
 * Regenerate by transcribing all Enable/Disable operations in reference
 * order, including repeated DAC updates and both protect calls. The oracle
 * below is independent of product output; tests require no external refs.
 *
 * The shared 3a device fake is the ONLY device model. regs.c is a separate
 * 3b work item, absent at this base. These contract adapters implement its
 * declared operations over d->io; they are not production regs coverage.
 */
#include "trident_device_fake.h"

#define TRY(op) do { int rc_ = (op); if (rc_) return rc_; } while (0)
int trident_write8(struct trident_device *d, enum trident_space space, u32 off, u8 value)
{
    return d->io->write8(d->io_opaque, space, off, value);
}
static int wr8(struct trident_device *d, u32 off, u8 value)
{
    return trident_write8(d, TRI_SPACE_REGS, off, value);
}
static int rd8(struct trident_device *d, u32 off, u8 *value)
{
    return d->io->read8(d->io_opaque, TRI_SPACE_REGS, off, value);
}
int trident_port8(struct trident_device *d, u16 port, u8 value)
{
    return d->io->port8(d->io_opaque, port, value);
}
int trident_indexed_read(struct trident_device *d, u16 port, u8 index, u8 *value)
{
    TRY(wr8(d, port, index));
    return rd8(d, port + 1, value);
}
int trident_indexed_write(struct trident_device *d, u16 port, u8 index, u8 value)
{
    return d->io->write16(d->io_opaque, TRI_SPACE_REGS, port,
                          index | ((u16)value << 8));
}
int trident_ext_dac_update(struct trident_device *d, u8 mask, u8 bits)
{
    u8 value;
    TRY(wr8(d, 0x83C8, 4));
    TRY(rd8(d, 0x83C6, &value));
    TRY(wr8(d, 0x83C8, 4));
    return wr8(d, 0x83C6, (value & mask) | bits);
}
int trident_protect(struct trident_device *d, u16 base, bool on)
{
    u8 seq, status;
    TRY(trident_indexed_read(d, 0x3C4, 1, &seq));
    if (on) {
        TRY(wr8(d, 0x3C4, 0)); TRY(wr8(d, 0x3C5, 1));
        TRY(wr8(d, 0x3C4, 1)); TRY(wr8(d, 0x3C5, seq | 0x20));
    } else {
        TRY(wr8(d, 0x3C4, 1)); TRY(wr8(d, 0x3C5, seq & 0xDF));
        TRY(wr8(d, 0x3C4, 0)); TRY(wr8(d, 0x3C5, 3));
    }
    TRY(rd8(d, base + 6, &status));
    return wr8(d, 0x3C0, on ? 0 : 0x20);
}

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
/* Enable/Disable/Enable has 45+53+45 accesses. Capacity is a host trace
 * budget, not a product retry/wait limit. */
#define TRACE_CAPACITY 256u
static struct trident_fake fake;
static struct trident_device device, before;
static struct trident_fake_event actual[TRACE_CAPACITY], expected[TRACE_CAPACITY];
static u8 storage[TRI_FAKE_REAL_BYTES];
static u32 expected_count;
static u8 seq, cr23, cr29, gr23, dac;
static u16 protect_base;

static void emit(const char *s)
{
    u32 len = 0;
    while (s[len]) ++len;
    __asm__ volatile ("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(len) : "memory");
}
static int red(const char *s) { emit("RED "); emit(s); emit("\n"); return 1; }
static bool equal(u32 a, u32 b)
{
    __asm__ volatile ("" : "+r"(a), "+r"(b) : : "memory");
    return a == b;
}
static void event(u32 space, u32 off, u32 value, u8 width, bool write)
{
    expected[expected_count++] = (struct trident_fake_event){
        .space = space, .offset = off, .value = value, .count = 1,
        .width = width, .write = write, .rc = 0
    };
}
static void w(u32 off, u8 value) { event(0, off, value, 1, true); }
static void r(u32 off, u8 value) { event(0, off, value, 1, false); }
static void p(u32 port, u8 value) { event(2, port, value, 1, true); }
/* Low-level oracle expansion, from vgaHW.c:574-604 and H:499-517. */
static void protect(bool on)
{
    w(0x3C4, 1); r(0x3C5, seq);
    if (on) {
        w(0x3C4, 0); w(0x3C5, 1);
        w(0x3C4, 1); seq |= 0x20; w(0x3C5, seq);
    } else {
        w(0x3C4, 1); seq &= 0xDF; w(0x3C5, seq);
        w(0x3C4, 0); w(0x3C5, 3);
    }
    /* Distinct status values make a color/mono substitution observable. */
    r(protect_base + 6, protect_base == 0x3B4 ? 0x39 : 0xD6);
    w(0x3C0, on ? 0 : 0x20);
}
static void indexed(u32 port, u8 index, u8 old, u8 value)
{
    w(port, index); r(port + 1, old);
    event(0, port, index | ((u16)value << 8), 2, true);
}
static void ext(u8 value)
{
    w(0x83C8, 4); r(0x83C6, dac); w(0x83C8, 4); w(0x83C6, value);
    dac = value;
}
/* These are the full frozen reference sequences, not a final-state oracle.
 * Keep separate ext() calls even when a combined RMW has the same result. */
static void oracle(bool enable)
{
    if (enable) {
        p(0x68, 0x0E); p(0x6A, 7); p(0x6A, 0x8F); p(0x6A, 6);
        protect(true);
        indexed(0x3D4, 0x23, cr23, cr23 & 0xDF); cr23 &= 0xDF;
        indexed(0x3D4, 0x29, cr29, cr29 | 4); cr29 |= 4;
        ext(dac | 6); ext(dac | 8);
        indexed(0x3CE, 0x23, gr23, gr23 & 0xFC); gr23 &= 0xFC;
        ext(dac | 1);
        indexed(0x3C4, 1, seq, seq & 0xEF); seq &= 0xEF;
        protect(false); p(0xFAC, 2);
    } else {
        p(0xFAC, 0); protect(true);
        indexed(0x3C4, 1, seq, seq | 0x10); seq |= 0x10;
        ext(dac & 0xFE);
        indexed(0x3CE, 0x23, gr23, (gr23 & 0xFC) | 1); gr23 = (gr23 & 0xFC) | 1;
        ext(dac & 0xFD); ext(dac & 0xCF); ext(dac & 0xF7); ext(dac & 0xFB);
        indexed(0x3D4, 0x29, cr29, cr29 & 0xFB); cr29 &= 0xFB;
        indexed(0x3D4, 0x23, cr23, cr23 | 0x20); cr23 |= 0x20;
        protect(false);
        p(0x6A, 7); p(0x6A, 0x8E); p(0x6A, 6); p(0x68, 0x0F);
    }
}
static int setup(u32 seed, u16 base)
{
    if (trident_fake_reset(&fake, storage, sizeof(storage), actual, ARRAY_COUNT(actual)))
        return red("reset");
    device = (struct trident_device){0};
    trident_fake_bind(&fake, &device);
    /* Lifecycle's prerequisites only; fake init/VRAM gate remains cold.
     * Standalone Disable models mode=2, without Save/PC98 Init. */
    device.pinned = true; device.armed = true;
    device.saved.vga_base = TRI_CR_MONO; /* Save(mono), then ModeInit/Restore. */
    device.cur_vga_base = base;
    /* ModeInit leaves color; Restore before standalone Disable restores mono. */
    fake.regs[TRI_MISC_WRITE] = base == TRI_CR_COLOR;
    device.last_rc = -71; device.restore_rc = -72; device.disable_rc = -73;
    device.step = TRI_STEP_MODE; device.failed_step = TRI_STEP_SAVE;
    before = device;
    protect_base = base;
    fake.sr[1] = seq = (u8)seed;
    fake.cr_color[0x23] = cr23 = (u8)(seed ^ 0xA5);
    fake.cr_color[0x29] = cr29 = (u8)(seed + 0x53);
    if (base == TRI_CR_MONO) cr23 = cr29 = 0xFF; /* color is inactive */
    fake.gr[0x23] = gr23 = (u8)(255 - seed);
    fake.ext_dac[4] = dac = (u8)(seed ^ 0x96);
    fake.ext_dac[5] = 0x69; /* wrong extended index sentinel */
    fake.regs[0x3BA] = 0x39; fake.regs[0x3DA] = 0xD6;
    expected_count = 0;
    return 0;
}
static bool same_event(const struct trident_fake_event *a,
                       const struct trident_fake_event *b, bool failed, int rc)
{
    return equal(a->space, b->space) && equal(a->offset, b->offset) &&
           equal(a->count, b->count) && equal(a->width, b->width) &&
           equal(a->write, b->write) && equal(a->rc, failed ? (u32)rc : 0) &&
           ((failed && !b->write) || equal(a->value, b->value));
}
static int trace_check(u32 count, int rc)
{
    if (!equal(fake.trace_count, count) || !equal(fake.operations, count) || fake.trace_overflow)
        return red("trace count / stop after failure");
    for (u32 i = 0; i < count; ++i)
        if (!same_event(&actual[i], &expected[i], rc && i + 1 == count, rc))
            return red("full ordered trace / width / RMW");
    const u8 *a = (const u8 *)&device, *b = (const u8 *)&before;
    for (u32 i = 0; i < sizeof(device); ++i)
        if (!equal(a[i], b[i])) return red("lifecycle state ownership");
    if (fake.pc98_initialized || fake.resources.ready || fake.ext_dac[5] != 0x69)
        return red("unrelated state");
    return 0;
}
static int invoke(bool enable)
{
    return enable ? trident_route_enable(&device) : trident_route_disable(&device);
}
static int success(void)
{
    for (u32 mono = 0; mono < 2; ++mono) {
        for (u32 seed = 0; seed < 256; ++seed) {
            /* mode=1 full cycle plus reentry; mode=2 Disable alone. */
            for (u32 standalone = 0; standalone < 2; ++standalone) {
                if (mono && !standalone) continue; /* Enable follows color ModeInit. */
                if (setup(seed, mono ? 0x3B4 : 0x3D4)) return 1;
                for (u32 step = 0; step < (standalone ? 1u : 3u); ++step) {
                    bool enable = !standalone && step != 1;
                    oracle(enable);
                    if (invoke(enable)) return red("success rc");
                    if (trace_check(expected_count, 0)) return 1;
                    if (!equal(fake.sr[1], seq) || !equal(fake.cr_color[0x23], mono ? (u8)(seed ^ 0xA5) : cr23) ||
                        !equal(fake.cr_color[0x29], mono ? (u8)(seed + 0x53) : cr29) || !equal(fake.gr[0x23], gr23) ||
                        !equal(fake.ext_dac[4], dac) || !equal(fake.sr[0], 3))
                        return red("final register values");
                }
            }
        }
    }
    return 0;
}
static int failures(void)
{
    /* All accesses, including helpers, both arbitrary negative rc values.
     * Expected trace comes from the oracle, not a successful product run. */
    for (u32 mono = 0; mono < 2; ++mono)
        for (u32 enable = 0; enable < 2; ++enable)
            for (u32 variant = 0; variant < 2; ++variant) {
                if (mono && enable) continue;
                u32 count;
                int rc = variant ? -197 : -43;
                if (setup(0xB7, mono ? 0x3B4 : 0x3D4)) return 1;
                oracle(enable); count = expected_count;
                for (u32 at = 1; at <= count; ++at) {
                    if (setup(0xB7, mono ? 0x3B4 : 0x3D4)) return 1;
                    oracle(enable);
                    trident_fake_fail(&fake, at, rc);
                    if (!equal(invoke(enable), (u32)rc)) return red("exact failure rc");
                    if (trace_check(at, rc)) return 1;
                }
            }
    return 0;
}
void _start(void)
{
    int rc = success();
    if (!rc) rc = failures();
    if (!rc && (trident_route_enable(0) != TRI_E_INVAL ||
                trident_route_disable(0) != TRI_E_INVAL)) rc = red("null device");
    if (!rc) emit("GREEN route: full traces, 256 seeds, mono/color, reentry, every failure\n");
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
