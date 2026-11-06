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
/* Init oracle: MIT trident_driver-1.4.0.c:616-692 and in-tree H:521-577.
 * License notices are retained in drivers/video/trident/pc98_init.c.
 * Recreate by transcribing each INB/OUTB/OUTW in order, expanding entry/exit
 * vgaHWProtect (vgaHW.c:474-488,574-604,1795-1806) and Unlock/Lock (:1795+).
 * vgaHW protect/lock retain Save IOBase; explicit D:650 AR and Trident CR
 * accesses remain color even when Save selected mono.
 * Expected events below never read product output or fake register state.
 */
#include "tools/tests/trident_device_fake.h"

/* Temporary link doubles for the separately assigned regs.c. These are
 * stateless contract adapters to the SHARED device fake, not another device
 * model. Production regs_bind/module assembly is tested in the wiring stage.
 */
int trident_read8(struct trident_device *d, enum trident_space s, u32 off, u8 *v)
{
    return d->io->read8(d->io_opaque, s, off, v);
}
int trident_write8(struct trident_device *d, enum trident_space s, u32 off, u8 v)
{
    return d->io->write8(d->io_opaque, s, off, v);
}
int trident_write16(struct trident_device *d, enum trident_space s, u32 off, u16 v)
{
    return d->io->write16(d->io_opaque, s, off, v);
}
#define DO(op) do { int rc = (op); if (rc) return rc; } while (0)
int trident_indexed_read(struct trident_device *d, u16 p, u8 i, u8 *v)
{
    DO(trident_write8(d, TRI_SPACE_REGS, p, i));
    return trident_read8(d, TRI_SPACE_REGS, p + 1, v);
}
int trident_indexed_write(struct trident_device *d, u16 p, u8 i, u8 v)
{
    return trident_write16(d, TRI_SPACE_REGS, p, i | ((u16)v << 8));
}
int trident_ar_write(struct trident_device *d, u16 base, u8 i, u8 v)
{
    u8 unused;
    DO(trident_read8(d, TRI_SPACE_REGS, base + 6, &unused));
    DO(trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, i));
    return trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, v);
}
int trident_crtc_lock(struct trident_device *d, u16 base, bool lock)
{
    u8 v;
    DO(trident_indexed_read(d, base, 0x11, &v));
    DO(trident_write8(d, TRI_SPACE_REGS, base, 0x11));
    return trident_write8(d, TRI_SPACE_REGS, base + 1,
                          lock ? v | 0x80 : v & 0x7F);
}
int trident_protect(struct trident_device *d, u16 base, bool on)
{
    u8 seq, unused;
    DO(trident_indexed_read(d, TRI_SR_INDEX, 1, &seq));
    if (on) {
        DO(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX, 0));
        DO(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX + 1, 1));
    }
    DO(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX, 1));
    DO(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX + 1,
                     on ? seq | 0x20 : seq & 0xDF));
    if (!on) {
        DO(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX, 0));
        DO(trident_write8(d, TRI_SPACE_REGS, TRI_SR_INDEX + 1, 3));
    }
    DO(trident_read8(d, TRI_SPACE_REGS, base + 6, &unused));
    return trident_write8(d, TRI_SPACE_REGS, TRI_AR_INDEX, on ? 0 : 0x20);
}
#undef DO

#define EVENT(off, val, bytes, wr) { .space = 0, .offset = (off), \
    .value = (val), .count = 1, .width = (bytes), .write = (wr), .rc = 0 }
#define R(off, val) EVENT(off, val, 1, false)
#define B(off, val) EVENT(off, val, 1, true)
#define W(off, val) EVENT(off, val, 2, true)
/* H:522-534 plus entry protect, independent seed-dependent read values. */
#define PREFIX \
    B(0x3C4, 1), R(0x3C5, seq), \
    B(0x3C4, 0), B(0x3C5, 1), B(0x3C4, 1), B(0x3C5, seq | 0x20), \
    R(base + 6, mono ? 0xA5 : 0x5A), B(0x3C0, 0), \
    R(0x3CC, misc), B(0x3C2, misc | 0xC3), \
    B(0x3C4, 0x0B), R(0x3C5, old), W(0x3C4, 0x0B | (old << 8)), \
    B(0x3C4, 0x0E), R(0x3C5, ext), W(0x3C4, 0x0E | ((ext | 0x20) << 8)), \
    B(0x3C4, 0x0C), R(0x3C5, config)
/* H:535-540; OUTB at D:636 discards ext, not an OUTW. */
#define CONFIG_PRIMARY \
    B(0x3C4, 0x0E), B(0x94, 0), B(0x102, 1), B(0x94, 0x20), \
    R(0x3C3, video), B(0x3C3, video | 1)
/* H:543-546; OUTB at D:643, all three alternate writes. */
#define CONFIG_ALTERNATE \
    B(0x3C4, 0x0E), B(0x46E8, 0x10), B(0x102, 1), B(0x46E8, 8)
/* H:548-576 plus exit protect. */
#define SUFFIX \
    R(0x3DA, 0x5A), B(0x3C0, 0x10), B(0x3C0, 0x41), \
    B(base, 0x11), R(base + 1, mono ? 0xFF : cr11), B(base, 0x11), B(base + 1, mono ? 0x7F : cr11 & 0x7F), \
    B(0x3C4, 0x0B), R(0x3C5, old), B(0x3C4, 0x0E), R(0x3C5, ext | 0x20), \
    W(0x3C4, 0x0E | ((ext | 0xA0) << 8)), \
    W(0x3D4, 0x3820), W(0x3D4, 0x2020), W(0x3D4, 0x0E30), \
    W(0x3D4, 0x0031), W(0x3D4, 0x0032), W(0x3D4, 0x213B), \
    B(0x43C6, 0xAF), B(0x43C7, 0), \
    B(0x3C4, 0x0B), R(0x3C5, old), B(0x3C4, 0x0E), R(0x3C5, ext | 0xA0), \
    W(0x3C4, 0x0E | (((ext | 0x20) & 0x7F) << 8)), \
    B(base, 0x11), R(base + 1, mono ? 0xFF : cr11 & 0x7F), B(base, 0x11), B(base + 1, mono ? 0xFF : cr11 | 0x80), \
    B(0x3C4, 1), R(0x3C5, seq | 0x20), B(0x3C4, 1), B(0x3C5, seq & 0xDF), \
    B(0x3C4, 0), B(0x3C5, 3), R(base + 6, mono ? 0xFF : 0x5A), B(0x3C0, 0x20)
#define LENGTH(a) (sizeof(a) / sizeof((a)[0]))
/* Capacity is derived from ALL fixture sections (both exclusive branches),
 * never from product trace length. Extra operations fail exact length checks. */
#define misc 0
#define seq 0
#define old 0
#define ext 0
#define config 0
#define video 0
#define base 0x3D4
#define mono 0
#define cr11 0
static const struct trident_fake_event sizing[] = {
    PREFIX, CONFIG_PRIMARY, CONFIG_ALTERNATE, SUFFIX
};
#undef misc
#undef seq
#undef old
#undef ext
#undef config
#undef video
#undef base
#undef mono
#undef cr11
static struct trident_fake_event expected[LENGTH(sizing)], trace[LENGTH(sizing)];
static struct trident_fake fake;
static struct trident_device device;
static u8 storage[TRI_FAKE_REAL_BYTES];

static int equal(u32 a, u32 b)
{
    __asm__ volatile ("" : "+r"(a), "+r"(b));
    return a == b;
}
#define REQUIRE(cond) do { if (!(cond)) return __LINE__; } while (0)
static u32 oracle(u8 config, bool mono, u8 seed)
{
    u8 misc = (seed & 0xFE) | !mono;
    u8 seq = seed ^ 0x37, old = seed ^ 0x6A, ext = seed ^ 0x45;
    u8 video = seed ^ 0x72, cr11 = seed ^ (mono ? 0xD7 : 0xB6);
    u16 base = mono ? 0x3B4 : 0x3D4;
    const struct trident_fake_event prefix[] = { PREFIX };
    const struct trident_fake_event primary[] = { CONFIG_PRIMARY };
    const struct trident_fake_event alternate[] = { CONFIG_ALTERNATE };
    const struct trident_fake_event suffix[] = { SUFFIX };
    u32 n = 0;
    for (u32 i = 0; i < LENGTH(prefix); ++i) expected[n++] = prefix[i];
    const struct trident_fake_event *branch = config & 0x10 ? primary : alternate;
    u32 count = config & 0x10 ? LENGTH(primary) : LENGTH(alternate);
    for (u32 i = 0; i < count; ++i) expected[n++] = branch[i];
    for (u32 i = 0; i < LENGTH(suffix); ++i) expected[n++] = suffix[i];
    return n;
}
static int setup(u8 config, bool mono, u8 seed)
{
    REQUIRE(!trident_fake_reset(&fake, storage, sizeof(storage), trace, LENGTH(trace)));
    device = (struct trident_device){0};
    trident_fake_bind(&fake, &device);
    REQUIRE(!device.init_done && !device.saved.valid && !fake.pc98_initialized);
    fake.regs[0x3C2] = (seed & 0xFE) | !mono;
    fake.sr[1] = seed ^ 0x37;
    fake.sr[0x0B] = seed ^ 0x6A;
    fake.sr[0x0C] = config;
    fake.sr[0x0E] = seed ^ 0x45;
    fake.regs[0x3C3] = seed ^ 0x72;
    fake.regs[0x3DA] = 0x5A;
    fake.regs[0x3BA] = 0xA5;
    fake.cr_color[0x11] = seed ^ 0xB6;
    fake.cr_mono[0x11] = seed ^ 0xD7;
    /* Save precedes Init; later MISC |= C3 must not change this base. */
    device.saved.vga_base = mono ? 0x3B4 : 0x3D4;
    device.last_rc = -91;
    device.step = TRI_STEP_SAVE;
    return 0;
}
static int unchanged_lifecycle(void)
{
    REQUIRE(!device.init_done && !device.preinit_done && !device.active);
    REQUIRE(!device.saved.valid && equal(device.step, TRI_STEP_SAVE));
    REQUIRE(device.last_rc == -91 && !device.failed_step);
    return 0;
}
static int compare(u32 count)
{
    REQUIRE(!fake.trace_overflow && equal(fake.trace_count, count));
    for (u32 i = 0; i < count; ++i) {
        const struct trident_fake_event *a = &trace[i], *b = &expected[i];
        REQUIRE(equal(a->space, b->space) && equal(a->offset, b->offset));
        REQUIRE(equal(a->value, b->value) && equal(a->width, b->width));
        REQUIRE(equal(a->count, b->count) && a->write == b->write && !a->rc);
    }
    REQUIRE(!fake.port_writes && !fake.reads[1] && !fake.reads[2]);
    REQUIRE(!fake.writes[2]);
    return 0;
}
static int check_gate(u32 count)
{
    u32 value;
    const struct trident_io *io = device.io;
    /* Even linear addressing remains blocked before independent Init accept. */
    REQUIRE(!fake.pc98_initialized);
    REQUIRE(!io->write16(&fake, TRI_SPACE_REGS, 0x3D4, 0x2021));
    REQUIRE(!io->write32(&fake, TRI_SPACE_LFB, 0, 0x12345678));
    REQUIRE(!io->read32(&fake, TRI_SPACE_LFB, 0, &value));
    REQUIRE(equal(value, 0xFA7F3F6F));
    REQUIRE(!trident_fake_accept_init(&fake, 0, expected, count));
    REQUIRE(!io->write32(&fake, TRI_SPACE_LFB, 0, 0x12345678));
    REQUIRE(!io->write32(&fake, TRI_SPACE_LFB, TRI_DISPLAY_BYTES - 4, 0x87654321));
    REQUIRE(!io->read32(&fake, TRI_SPACE_LFB, 0, &value));
    REQUIRE(equal(value, 0x12345678));
    REQUIRE(!io->read32(&fake, TRI_SPACE_LFB, TRI_DISPLAY_BYTES - 4, &value));
    REQUIRE(equal(value, 0x87654321));
    return 0;
}
static int selftest(void)
{
    /* Every SR0C byte: not just the 0/10 examples. Seeds exercise all bits in
     * MISC, SR01, SR0B, SR0E, CR11 and video-enable RMW values, both bases. */
    for (u32 mono = 0; mono < 2; ++mono) {
        for (u32 config = 0; config < 256; ++config) {
            REQUIRE(!setup(config, mono, config));
            u32 count = oracle(config, mono, config);
            REQUIRE(!trident_pc98_init(&device));
            REQUIRE(!compare(count));
            REQUIRE(!unchanged_lifecycle());
            REQUIRE(equal(device.saved.vga_base, mono ? 0x3B4 : 0x3D4));
            REQUIRE(equal(fake.cr_mono[0x11],
                          (u8)(config ^ 0xD7)));
            REQUIRE(equal(fake.cr_color[0x11],
                          (u8)((config ^ 0xB6) | (mono ? 0 : 0x80))));
            if (config == 0 || config == 0x10) REQUIRE(!check_gate(count));
        }
        for (u32 branch = 0; branch < 2; ++branch) {
            u8 config = branch ? 0x10 : 0;
            u32 count = oracle(config, mono, 0);
            for (u32 at = 1; at <= count; ++at) {
                REQUIRE(!setup(config, mono, 0));
                trident_fake_fail(&fake, at, -73);
                REQUIRE(trident_pc98_init(&device) == -73);
                REQUIRE(equal(fake.operations, at) && equal(fake.trace_count, at));
                REQUIRE(trace[at - 1].rc == -73 && !fake.trace_overflow);
                REQUIRE(!fake.pc98_initialized && !unchanged_lifecycle());
                /* Every earlier operation remains an exact reference prefix. */
                for (u32 i = 0; i + 1 < at; ++i) {
                    REQUIRE(equal(trace[i].offset, expected[i].offset));
                    REQUIRE(equal(trace[i].value, expected[i].value));
                    REQUIRE(equal(trace[i].width, expected[i].width));
                    REQUIRE(trace[i].write == expected[i].write && !trace[i].rc);
                }
            }
        }
    }
    return 0;
}
static void emit(const char *s, u32 n)
{
    u32 call = 4;
    __asm__ volatile ("int $0x80" : "+a"(call) : "b"(1), "c"(s), "d"(n)
                      : "memory", "cc");
}
void _start(void)
{
    int rc = selftest();
    if (rc) {
        char digits[10];
        u32 n = 0, line = (u32)rc;
        emit("RED pc98init line ", sizeof("RED pc98init line ") - 1);
        do { digits[n++] = '0' + line % 10; line /= 10; } while (line);
        while (n) emit(&digits[--n], 1);
        emit("\n", 1);
    } else emit("GREEN pc98init\n", sizeof("GREEN pc98init\n") - 1);
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
