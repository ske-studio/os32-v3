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
/* Save oracle transcribed from trident_mode.hwb:587-634,1404-1496.
 * DAC fixture: H:636-1403 = vgaHW.c:90-155. Regenerate the initializer
 * from H's 768 "let save[index] = value" lines, in index order.
 * Access wrappers stand in for parallel 3b regs.c, not a second device fake.
 */
#include "tools/tests/trident_device_fake.h"
#include "drivers/char/serial.h"
const u8 trident_default_dac[TRI_DAC_BYTES] = {
    0, 0, 0, 0, 0, 42, 0, 42, 0, 0, 42, 42, 42, 0, 0, 42, 0, 42, 42, 21, 0, 42, 42, 42,
    21, 21, 21, 21, 21, 63, 21, 63, 21, 21, 63, 63, 63, 21, 21, 63, 21, 63, 63, 63, 21, 63, 63, 63,
    0, 0, 0, 5, 5, 5, 8, 8, 8, 11, 11, 11, 14, 14, 14, 17, 17, 17, 20, 20, 20, 24, 24, 24,
    28, 28, 28, 32, 32, 32, 36, 36, 36, 40, 40, 40, 45, 45, 45, 50, 50, 50, 56, 56, 56, 63, 63, 63,
    0, 0, 63, 16, 0, 63, 31, 0, 63, 47, 0, 63, 63, 0, 63, 63, 0, 47, 63, 0, 31, 63, 0, 16,
    63, 0, 0, 63, 16, 0, 63, 31, 0, 63, 47, 0, 63, 63, 0, 47, 63, 0, 31, 63, 0, 16, 63, 0,
    0, 63, 0, 0, 63, 16, 0, 63, 31, 0, 63, 47, 0, 63, 63, 0, 47, 63, 0, 31, 63, 0, 16, 63,
    31, 31, 63, 39, 31, 63, 47, 31, 63, 55, 31, 63, 63, 31, 63, 63, 31, 55, 63, 31, 47, 63, 31, 39,
    63, 31, 31, 63, 39, 31, 63, 47, 31, 63, 55, 31, 63, 63, 31, 55, 63, 31, 47, 63, 31, 39, 63, 31,
    31, 63, 31, 31, 63, 39, 31, 63, 47, 31, 63, 55, 31, 63, 63, 31, 55, 63, 31, 47, 63, 31, 39, 63,
    45, 45, 63, 49, 45, 63, 54, 45, 63, 58, 45, 63, 63, 45, 63, 63, 45, 58, 63, 45, 54, 63, 45, 49,
    63, 45, 45, 63, 49, 45, 63, 54, 45, 63, 58, 45, 63, 63, 45, 58, 63, 45, 54, 63, 45, 49, 63, 45,
    45, 63, 45, 45, 63, 49, 45, 63, 54, 45, 63, 58, 45, 63, 63, 45, 58, 63, 45, 54, 63, 45, 49, 63,
    0, 0, 28, 7, 0, 28, 14, 0, 28, 21, 0, 28, 28, 0, 28, 28, 0, 21, 28, 0, 14, 28, 0, 7,
    28, 0, 0, 28, 7, 0, 28, 14, 0, 28, 21, 0, 28, 28, 0, 21, 28, 0, 14, 28, 0, 7, 28, 0,
    0, 28, 0, 0, 28, 7, 0, 28, 14, 0, 28, 21, 0, 28, 28, 0, 21, 28, 0, 14, 28, 0, 7, 28,
    14, 14, 28, 17, 14, 28, 21, 14, 28, 24, 14, 28, 28, 14, 28, 28, 14, 24, 28, 14, 21, 28, 14, 17,
    28, 14, 14, 28, 17, 14, 28, 21, 14, 28, 24, 14, 28, 28, 14, 24, 28, 14, 21, 28, 14, 17, 28, 14,
    14, 28, 14, 14, 28, 17, 14, 28, 21, 14, 28, 24, 14, 28, 28, 14, 24, 28, 14, 21, 28, 14, 17, 28,
    20, 20, 28, 22, 20, 28, 24, 20, 28, 26, 20, 28, 28, 20, 28, 28, 20, 26, 28, 20, 24, 28, 20, 22,
    28, 20, 20, 28, 22, 20, 28, 24, 20, 28, 26, 20, 28, 28, 20, 26, 28, 20, 24, 28, 20, 22, 28, 20,
    20, 28, 20, 20, 28, 22, 20, 28, 24, 20, 28, 26, 20, 28, 28, 20, 26, 28, 20, 24, 28, 20, 22, 28,
    0, 0, 16, 4, 0, 16, 8, 0, 16, 12, 0, 16, 16, 0, 16, 16, 0, 12, 16, 0, 8, 16, 0, 4,
    16, 0, 0, 16, 4, 0, 16, 8, 0, 16, 12, 0, 16, 16, 0, 12, 16, 0, 8, 16, 0, 4, 16, 0,
    0, 16, 0, 0, 16, 4, 0, 16, 8, 0, 16, 12, 0, 16, 16, 0, 12, 16, 0, 8, 16, 0, 4, 16,
    8, 8, 16, 10, 8, 16, 12, 8, 16, 14, 8, 16, 16, 8, 16, 16, 8, 14, 16, 8, 12, 16, 8, 10,
    16, 8, 8, 16, 10, 8, 16, 12, 8, 16, 14, 8, 16, 16, 8, 14, 16, 8, 12, 16, 8, 10, 16, 8,
    8, 16, 8, 8, 16, 10, 8, 16, 12, 8, 16, 14, 8, 16, 16, 8, 14, 16, 8, 12, 16, 8, 10, 16,
    11, 11, 16, 12, 11, 16, 13, 11, 16, 15, 11, 16, 16, 11, 16, 16, 11, 15, 16, 11, 13, 16, 11, 12,
    16, 11, 11, 16, 12, 11, 16, 13, 11, 16, 15, 11, 16, 16, 11, 15, 16, 11, 13, 16, 11, 12, 16, 11,
    11, 16, 11, 11, 16, 12, 11, 16, 13, 11, 16, 15, 11, 16, 16, 11, 15, 16, 11, 13, 16, 11, 12, 16,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static struct trident_fake fake;
static struct trident_device device;
static u8 storage[TRI_FAKE_REAL_BYTES];
/* H:587-1496 expands to <2600 accesses; headroom is for trace capture only. */
static struct trident_fake_event trace[4096];
static u8 boot_dac[768], boot_cr[256], boot_ar[32], boot_gr[256], boot_sr[256];
static u32 diagnostics, cursor;
static int oracle_error;
static void zero(void *p, u32 n) { for (u32 i=0;i<n;++i) ((u8 *)p)[i]=0; }
static int equal(u32 a, u32 b)
{
    __asm__ volatile ("" : "+r"(a), "+r"(b) : : "memory");
    return a == b;
}
#define REQUIRE(c) do { if (!(c)) return __LINE__; } while (0)
void serial_puts(const char *s)
{
    static const char expected[] = "[trident] stage=save-palette rc=-6 default DAC\n";
    u32 i=0;
    while (s[i] && s[i]==expected[i]) ++i;
    if (s[i] || expected[i]) oracle_error=1;
    ++diagnostics;
}
int trident_read8(struct trident_device *d, enum trident_space s, u32 off, u8 *v)
{ return d->io->read8(d->io_opaque,s,off,v); }
int trident_write8(struct trident_device *d, enum trident_space s, u32 off, u8 v)
{ return d->io->write8(d->io_opaque,s,off,v); }
int trident_indexed_read(struct trident_device *d, u16 p, u8 i, u8 *v)
{
    int rc=trident_write8(d,TRI_SPACE_REGS,p,i);
    return rc ? rc : trident_read8(d,TRI_SPACE_REGS,p+1,v);
}
int trident_indexed_write(struct trident_device *d, u16 p, u8 i, u8 v)
{ return d->io->write16(d->io_opaque,TRI_SPACE_REGS,p,i | ((u16)v<<8)); }
int trident_ar_read(struct trident_device *d, u16 base, u8 i, u8 *v)
{
    u8 ignored;
    int rc=trident_read8(d,TRI_SPACE_REGS,base+6,&ignored);
    if (rc) return rc;
    rc=trident_write8(d,TRI_SPACE_REGS,0x3C0,i);
    return rc ? rc : trident_read8(d,TRI_SPACE_REGS,0x3C1,v);
}
int trident_hidden_dac_read(struct trident_device *d, u8 *v)
{
    u8 ignored;
    int rc=trident_read8(d,TRI_SPACE_REGS,0x3C8,&ignored);
    if (rc) return rc;
    for (u32 i=0;i<4;++i) {
        rc=trident_read8(d,TRI_SPACE_REGS,0x3C6,&ignored);
        if (rc) return rc;
    }
    rc=trident_read8(d,TRI_SPACE_REGS,0x3C6,v);
    return rc ? rc : trident_read8(d,TRI_SPACE_REGS,0x3C8,&ignored);
}
static int seed(bool color, bool readable)
{
    zero(&device,sizeof(device)); diagnostics=cursor=0; oracle_error=0;
    REQUIRE(!trident_fake_reset(&fake,storage,sizeof(storage),trace,4096));
    trident_fake_bind(&fake,&device);
    /* Keep production defaults (Command=2, SR09=10, GR06=0C, SPR=7F,
     * blocked BAR0) and seed only unobserved save registers distinctly. */
    for (u32 i=0;i<256;++i) {
        if (i!=9) fake.sr[i]=(i*3+17)&255;
        if (i!=6) fake.gr[i]=(i*5+7)&255;
        if (i!=0x1F) {
            fake.cr_color[i]=(i*7+31)&255;
            fake.cr_mono[i]=(i*11+41)&255;
        }
    }
    for (u32 i=0;i<32;++i) fake.ar[i]=(i*13+3)&255;
    for (u32 i=0;i<768;++i) boot_dac[i]=fake.dac[i]=(i*17+i/3+5)&63;
    fake.dac_readable=readable;
    fake.regs[0x3C2]=color ? 0xA7 : 0xA6;
    fake.regs[0x43C8]=0x46; fake.regs[0x43C9]=0xB9;
    fake.regs[0x43C6]=0x98; fake.regs[0x43C7]=0x23;
    fake.hidden_dac=0x5A;
    for (u32 i=0;i<256;++i) {
        boot_cr[i]=color ? fake.cr_color[i] : fake.cr_mono[i];
        boot_gr[i]=fake.gr[i]; boot_sr[i]=fake.sr[i];
    }
    for (u32 i=0;i<32;++i) boot_ar[i]=fake.ar[i];
    return 0;
}
static void event(bool write, u8 width, u32 off, u32 value)
{
    if (cursor>=fake.trace_count) { oracle_error=1; return; }
    const struct trident_fake_event *e=&trace[cursor++];
    if (e->space!=0 || e->write!=write || e->width!=width ||
        e->offset!=off || e->value!=value || e->rc || e->count!=1)
        oracle_error=1;
}
#define R(p,v) event(false,1,p,v)
#define W(p,v) event(true,1,p,v)
static void indexed(u16 p,u8 i,u8 v) { W(p,i); R(p+1,v); }
static void attr(u16 base,u8 v) { R(base+6,0); W(0x3C0,v); }
static int oracle(bool color,bool readable)
{
    u16 base=color ? 0x3D4 : 0x3B4;
    u8 misc=color ? 0xA7 : 0xA6;
    static const u8 extended[]={0x13,0x21,0x1E,0x27,0x2B,0x2F,0x2A,
                                0x3A,0x29,0x38,0x36,0x39,0x55,0x50};
    R(0x3CC,misc); W(0x3C6,255); W(0x3C7,0);
    for (u32 i=0;i<6;++i) R(0x3C9,readable ? boot_dac[i] : 255);
    W(0x3C8,1);
    for (u32 i=3;i<6;++i) W(0x3C9,readable ? (boot_dac[i]^63) : 0);
    W(0x3C7,1);
    for (u32 i=3;i<6;++i) R(0x3C9,readable ? (boot_dac[i]^63) : 255);
    W(0x3C8,1);
    for (u32 i=3;i<6;++i) W(0x3C9,readable ? boot_dac[i] : 255);
    if (readable) {
        W(0x3C7,2);
        for (u32 i=6;i<768;++i) { R(0x3C9,boot_dac[i]); R(base+6,0); R(base+6,0); }
    }
    attr(base,0x20); R(0x3CC,misc);
    for (u32 i=0;i<25;++i) indexed(base,i,boot_cr[i]);
    attr(base,0);
    for (u32 i=0;i<21;++i) { R(base+6,0); W(0x3C0,i); R(0x3C1,boot_ar[i]); }
    attr(base,0x20);
    for (u32 i=0;i<9;++i) indexed(0x3CE,i,boot_gr[i]);
    for (u32 i=1;i<5;++i) indexed(0x3C4,i,boot_sr[i]);
    indexed(0x3C4,0x0B,boot_sr[0x0B]); indexed(0x3C4,0x0E,boot_sr[0x0E]);
    event(true,2,0x3C4,0xC20E);
    for (u32 i=0;i<sizeof(extended);++i) indexed(base,extended[i],boot_cr[extended[i]]);
    indexed(0x3CE,0x0F,boot_gr[0x0F]); indexed(0x3CE,0x2F,boot_gr[0x2F]);
    R(0x3C8,1);
    for (u32 i=0;i<4;++i) R(0x3C6,255);
    R(0x3C6,0x5A); R(0x3C8,1);
    R(0x3CC,misc); R(0x43C8,0x46); R(0x43C9,0xB9);
    indexed(0x3C4,0x0D,boot_sr[0x0D]);
    event(true,2,0x3C4,0x0E | ((u16)boot_sr[0x0E]<<8));
    REQUIRE(!oracle_error && equal(cursor,fake.trace_count) && !fake.trace_overflow);
    /* Independent slot oracle, including GR06, CR3A and the last DAC byte. */
    const u8 *s=device.saved.value;
    for (u32 i=0;i<768;++i) REQUIRE(equal(s[i],readable ? boot_dac[i] : trident_default_dac[i]));
    REQUIRE(equal(s[768],misc));
    for (u32 i=0;i<25;++i) REQUIRE(equal(s[769+i],boot_cr[i]));
    for (u32 i=0;i<21;++i) REQUIRE(equal(s[794+i],boot_ar[i]));
    for (u32 i=0;i<9;++i) REQUIRE(equal(s[815+i],boot_gr[i]));
    for (u32 i=1;i<5;++i) REQUIRE(equal(s[823+i],boot_sr[i]));
    REQUIRE(equal(s[828],boot_sr[14]));
    for (u32 i=0;i<sizeof(extended);++i) REQUIRE(equal(s[829+i],boot_cr[extended[i]]));
    REQUIRE(equal(s[843],boot_gr[15]) && equal(s[844],boot_gr[47]));
    REQUIRE(equal(s[845],0x5A) && equal(s[846],misc));
    REQUIRE(equal(s[847],0x46) && equal(s[848],0xB9));
    REQUIRE(equal(s[849],0) && equal(s[850],0) && equal(s[851],boot_sr[13]));
    REQUIRE(device.saved.valid && equal(device.saved.vga_base,base));
    REQUIRE(equal(diagnostics,readable ? 0 : 1));
    REQUIRE(equal(fake.sr[14],boot_sr[14]) && equal(fake.gr[6],12));
    if (readable) for (u32 i=0;i<768;++i) REQUIRE(equal(fake.dac[i],boot_dac[i]));
    REQUIRE(!device.init_done && !device.first_init_done && !fake.pc98_initialized);
    REQUIRE(!fake.reads[1] && !fake.reads[2] && !fake.writes[2] && !fake.port_writes);
    return 0;
}
static int selftest(void)
{
    REQUIRE(equal(trident_state_save(0),TRI_E_INVAL));
    for (u32 color=0;color<2;++color) for (u32 readable=0;readable<2;++readable) {
        REQUIRE(!seed(color,readable));
        device.last_rc=-123; device.failed_step=7;
        REQUIRE(!trident_state_save(&device));
        int rc=oracle(color,readable); if (rc) return rc;
        REQUIRE(equal(device.last_rc,(u32)-123) && equal(device.failed_step,7));
        u32 count=fake.operations;
        /* Simulate changed mode before a second call: saved boot AR10/CR11
         * and all remaining bytes must remain immutable, with no access. */
        fake.ar[0x10]^=255; fake.cr_color[0x11]^=255; fake.cr_mono[0x11]^=255;
        struct trident_saved copy=device.saved;
        REQUIRE(!trident_state_save(&device) && equal(fake.operations,count));
        for (u32 i=0;i<852;++i) REQUIRE(equal(copy.value[i],device.saved.value[i]));
    }
    /* Each actual access can fail: preserve exact rc, do not commit valid. */
    REQUIRE(!seed(true,true)); REQUIRE(!trident_state_save(&device));
    u32 total=fake.operations;
    for (u32 op=1;op<=total;++op) {
        REQUIRE(!seed(true,true));
        trident_fake_fail(&fake,op,-73);
        REQUIRE(equal(trident_state_save(&device),(u32)-73));
        REQUIRE(!device.saved.valid && equal(fake.operations,op));
    }
    return 0;
}
static void emit(const char *s,u32 n)
{ __asm__ volatile ("int $0x80" : : "a"(4),"b"(1),"c"(s),"d"(n) : "memory"); }
void _start(void)
{
    int rc=selftest();
    if (rc) {
        char msg[]="RED save line 0000\n";
        u32 n=rc;
        for (u32 i=0;i<4;++i) { msg[17-i]='0'+n%10; n/=10; }
        emit(msg,sizeof(msg)-1);
    } else emit("GREEN\n",6);
    __asm__ volatile ("int $0x80" : : "a"(1),"b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
