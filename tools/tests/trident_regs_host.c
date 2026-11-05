/* libc-free regs tests. Independent trace fixtures from MIT trident_regs.h:
 * 302-325, vgaHW.c:574-604,1795-1806, trident_dac.c:781-791,1009-1016,
 * and trident_mode.hwb:493-518,1421-1423,1478-1484,1557-1559.
 * Recreate by transcribing those access sequences, never product output.
 * The product's retained source notices apply to these sequence fixtures.
 */
#include "tools/tests/trident_device_fake.h"
/* Replace only the privileged platform primitive. Include the real TU so
 * bind, all production callbacks and pointer/range logic run in this test. */
#define IO_H
static u32 port_calls, port_at, port_value;
static void outp(unsigned int port, unsigned int value)
{
    ++port_calls; port_at = port; port_value = value;
}
#include "drivers/video/trident/regs.c"

static struct trident_fake f;
static struct trident_device d;
static struct trident_fake_event trace[32];
static u8 storage[TRI_FAKE_REAL_BYTES];
static u8 result;
static int equal(u32 a, u32 b)
{
    __asm__ volatile ("" : "+r"(a), "+r"(b) : : "memory");
    return a == b;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int reset(void)
{
    d = (struct trident_device){0};
    int rc = trident_fake_reset(&f, storage, sizeof(storage), trace, 32);
    if (rc) return rc;
    trident_fake_bind(&f, &d);
    f.sr[1] = 0xB7;
    f.cr_color[0x11] = f.cr_mono[0x11] = 0x55;
    f.ar[0x12] = 0x35;
    f.hidden_dac = 0xA6;
    f.ext_dac[4] = 0x9A;
    result = 0xED;
    return 0;
}
static int invoke(u32 which, u16 cb)
{
    switch (which) {
    case 0: return trident_indexed_read(&d, TRI_SR_INDEX, 1, &result);
    case 1: return trident_indexed_write(&d, TRI_SR_INDEX, 1, 0x5B);
    case 2: return trident_ar_read(&d, cb, 0x12, &result);
    case 3: return trident_ar_write(&d, cb, 0x12, 0x5B);
    case 4: return trident_hidden_dac_read(&d, &result);
    case 5: return trident_hidden_dac_write(&d, 0x5B);
    case 6: return trident_ext_dac_update(&d, 0xCF, 0x21);
    case 7: return trident_protect(&d, cb, true);
    case 8: return trident_protect(&d, cb, false);
    case 9: return trident_crtc_lock(&d, cb, true);
    default: return trident_crtc_lock(&d, cb, false);
    }
}
#define R(off, val) {0, (off), (val), 1, 1, false, 0}
#define W(off, val) {0, (off), (val), 1, 1, true, 0}
static int sequences(u16 cb)
{
    const struct trident_fake_event fixtures[][8] = {
        {W(0x3C4,1), R(0x3C5,0xB7)},
        {{0,0x3C4,0x5B01,1,2,true,0}},
        {R(cb+6,0), W(0x3C0,0x12), R(0x3C1,0x35)},
        {R(cb+6,0), W(0x3C0,0x12), W(0x3C0,0x5B)},
        {R(0x3C8,0), R(0x3C6,0), R(0x3C6,0), R(0x3C6,0), R(0x3C6,0),
         R(0x3C6,0xA6), R(0x3C8,0)},
        {R(0x3C8,0), R(0x3C6,0), R(0x3C6,0), R(0x3C6,0), R(0x3C6,0),
         W(0x3C6,0x5B), R(0x3C8,0)},
        {W(0x83C8,4), R(0x83C6,0x9A), W(0x83C8,4), W(0x83C6,0xAB)},
        {W(0x3C4,1), R(0x3C5,0xB7), W(0x3C4,0), W(0x3C5,1),
         W(0x3C4,1), W(0x3C5,0xB7), R(cb+6,0), W(0x3C0,0)},
        {W(0x3C4,1), R(0x3C5,0xB7), W(0x3C4,1), W(0x3C5,0x97),
         W(0x3C4,0), W(0x3C5,3), R(cb+6,0), W(0x3C0,0x20)},
        {W(cb,0x11), R(cb+1,0x55), W(cb,0x11), W(cb+1,0xD5)},
        {W(cb,0x11), R(cb+1,0x55), W(cb,0x11), W(cb+1,0x55)}
    };
    const u32 counts[] = {2,1,3,3,7,7,4,8,8,4,4};
    for (u32 which = 0; which < sizeof(counts)/sizeof(counts[0]); ++which) {
        CHECK(!reset());
        f.regs[TRI_MISC_WRITE] = cb == TRI_CR_COLOR;
        CHECK(!invoke(which, cb));
        CHECK(equal(f.trace_count, counts[which]) && !f.trace_overflow);
        for (u32 i = 0; i < counts[which]; ++i) {
            const struct trident_fake_event *a = &trace[i], *b = &fixtures[which][i];
            CHECK(equal(a->space,b->space) && equal(a->offset,b->offset));
            CHECK(equal(a->value,b->value) && equal(a->width,b->width));
            CHECK(equal(a->write,b->write) && equal(a->count,1) && !a->rc);
        }
        CHECK(!f.pc98_initialized && !d.armed && !d.pinned);
        if (which == 0) CHECK(equal(result,0xB7));
        if (which == 2) CHECK(equal(result,0x35));
        if (which == 3) CHECK(equal(f.ar[0x12],0x5B));
        if (which == 4) CHECK(equal(result,0xA6) && !f.hidden_reads);
        if (which == 5) CHECK(equal(f.hidden_dac,0x5B) && !f.hidden_reads);
        /* Every access can fail, including trailing hidden-DAC reset reads.
         * The exact rc returns immediately; failed reads never publish output. */
        for (u32 fail = 1; fail <= counts[which]; ++fail) {
            CHECK(!reset());
        f.regs[TRI_MISC_WRITE] = cb == TRI_CR_COLOR;
            trident_fake_fail(&f, fail, -73);
            CHECK(equal(invoke(which,cb), (u32)-73));
            CHECK(equal(f.operations,fail) && equal(f.trace_count,fail));
            CHECK(equal(trace[fail-1].rc,(u32)-73));
            if (which == 0 || which == 2 || which == 4) CHECK(equal(result,0xED));
        }
    }
    return 0;
}
static int scalar_fake(void)
{
    CHECK(!reset());
    f.trace_lfb = true;
    u8 a = 0xED; u16 b = 0xED; u32 c = 0xED;
    CHECK(!trident_write8(&d,TRI_SPACE_REGS,0x100,0x5A));
    CHECK(!trident_write16(&d,TRI_SPACE_REGS,0x102,0xB67C));
    CHECK(!trident_write32(&d,TRI_SPACE_REGS,0x104,0x89ABCDEF));
    CHECK(!trident_read8(&d,TRI_SPACE_REGS,0x100,&a) && equal(a,0x5A));
    CHECK(!trident_read16(&d,TRI_SPACE_REGS,0x102,&b) && equal(b,0xB67C));
    CHECK(!trident_read32(&d,TRI_SPACE_REGS,0x104,&c) && equal(c,0x89ABCDEF));
    for (u32 i=0; i<3; ++i) {
        CHECK(equal(f.reads[i],1) && equal(f.writes[i],1));
        CHECK(equal(trace[i].width,1u<<i) && equal(trace[i+3].width,1u<<i));
    }
    CHECK(!trident_write32(&d,TRI_SPACE_LFB,0,0x12345678));
    CHECK(!trident_read32(&d,TRI_SPACE_LFB,0,&c));
    CHECK(equal(c,0xFA7F3F6F)); /* observed blocked value, gate remains closed */
    CHECK(!trident_port8(&d,0xFAC,2));
    CHECK(equal(f.last_port,0xFAC) && equal(f.last_port_value,2));
    CHECK(equal(trace[8].space,2) && equal(trace[8].width,1));
    trident_fake_fail(&f,f.operations+1,-79);
    CHECK(equal(trident_port8(&d,0xFAC,0),(u32)-79));
    CHECK(equal(f.last_port_value,2));
#define FAIL_READ(bits, value) do { \
    trident_fake_fail(&f,f.operations+1,-81); \
    CHECK(equal(trident_read##bits(&d,TRI_SPACE_REGS,0x100,&value),(u32)-81)); \
    CHECK(equal(value,0xED)); \
} while (0)
    a=0xED; b=0xED; c=0xED;
    FAIL_READ(8,a); FAIL_READ(16,b); FAIL_READ(32,c);
#define FAIL_WRITE(bits) do { \
    trident_fake_fail(&f,f.operations+1,-83); \
    CHECK(equal(trident_write##bits(&d,TRI_SPACE_REGS,0x100,0),(u32)-83)); \
} while (0)
    FAIL_WRITE(8); FAIL_WRITE(16); FAIL_WRITE(32);
    CHECK(equal(f.regs[0x100],0x5A));
    return 0;
}
static int production(void)
{
    static u32 regs[32], lfb[32];
    const u32 src[] = {0xABCDEF01,0x23456789,0xBADC0FFE};
    struct trident_resources r = {.regs=(volatile u8 *)regs,
        .lfb=(volatile u8 *)lfb, .regs_bytes=sizeof(regs),
        .mapped_bytes=sizeof(lfb), .decode_bytes=sizeof(lfb)};
    struct trident_device p = {.res=&r};
    CHECK(!trident_regs_bind(&p));
    CHECK(p.io && p.io_opaque == &r && !port_calls);
    CHECK(!regs[0] && !lfb[0] && !p.armed && !p.pinned);
    CHECK(p.io->read8 && p.io->read16 && p.io->read32 && p.io->write8 &&
          p.io->write16 && p.io->write32 && p.io->port8 && p.io->write_span32);
    for (u32 s=0; s<2; ++s) {
        u8 a=0; u16 b=0; u32 c=0;
        CHECK(!trident_write32(&p,s,0,0x12345678));
        CHECK(!trident_read32(&p,s,0,&c) && equal(c,0x12345678));
        CHECK(!trident_read16(&p,s,2,&b) && equal(b,0x1234));
        CHECK(!trident_read8(&p,s,1,&a) && equal(a,0x56));
        CHECK(!trident_write8(&p,s,1,0xAB));
        CHECK(!trident_write16(&p,s,2,0xCDEF));
        CHECK(!trident_read32(&p,s,0,&c) && equal(c,0xCDEFAB78));
        CHECK(!trident_write8(&p,s,127,0x5A));
        CHECK(!trident_write16(&p,s,126,0xB67C));
        CHECK(!trident_write32(&p,s,124,0x89ABCDEF));
        CHECK(!trident_read8(&p,s,127,&a) && equal(a,0x89));
        CHECK(!trident_read16(&p,s,126,&b) && equal(b,0x89AB));
        CHECK(!trident_read32(&p,s,124,&c) && equal(c,0x89ABCDEF));
        a=0xED; b=0xED; c=0xED;
        CHECK(trident_read8(&p,s,128,&a)==TRI_E_ACCESS && equal(a,0xED));
        CHECK(trident_read16(&p,s,127,&b)==TRI_E_ACCESS && equal(b,0xED));
        CHECK(trident_read32(&p,s,126,&c)==TRI_E_ACCESS && equal(c,0xED));
        CHECK(trident_write8(&p,s,U32_MAX,0)==TRI_E_ACCESS);
        CHECK(trident_write16(&p,s,1,0)==TRI_E_ACCESS);
        CHECK(trident_write32(&p,s,2,0)==TRI_E_ACCESS);
        CHECK(trident_write32(&p,s,128,0)==TRI_E_ACCESS);
    }
    CHECK(!p.io->write_span32(p.io_opaque,116,src,3));
    CHECK(equal(lfb[29],src[0]) && equal(lfb[30],src[1]) && equal(lfb[31],src[2]));
    CHECK(equal(lfb[28],0));
    CHECK(p.io->write_span32(p.io_opaque,120,src,3)==TRI_E_ACCESS);
    CHECK(equal(lfb[30],src[1])); /* reject whole row before first write */
    CHECK(p.io->write_span32(p.io_opaque,0,src,U32_MAX)==TRI_E_ACCESS);
    CHECK(p.io->write_span32(p.io_opaque,1,src,1)==TRI_E_ACCESS);
    CHECK(p.io->write_span32(p.io_opaque,0,(const u32 *)((const u8 *)src+1),1)==TRI_E_ACCESS);
    CHECK(p.io->write_span32(p.io_opaque,0,0,1)==TRI_E_ACCESS);
    CHECK(p.io->write_span32(p.io_opaque,0,src,0)==TRI_E_ACCESS);
    r.decode_bytes=120;
    CHECK(trident_write32(&p,TRI_SPACE_LFB,120,0)==TRI_E_ACCESS);
    CHECK(p.io->write_span32(p.io_opaque,116,src,2)==TRI_E_ACCESS);
    r.decode_bytes=128; r.mapped_bytes=120;
    CHECK(trident_write32(&p,TRI_SPACE_LFB,120,0)==TRI_E_ACCESS);
    CHECK(p.io->write_span32(p.io_opaque,116,src,2)==TRI_E_ACCESS);
    CHECK(!trident_port8(&p,0xFAC,2));
    CHECK(equal(port_calls,1) && equal(port_at,0xFAC) && equal(port_value,2));
    u8 a=0xED;
    CHECK(trident_read8(&p,2,0,&a)==TRI_E_ACCESS && equal(a,0xED));
    CHECK(trident_read8(&p,0,0,0)==TRI_E_INVAL);
    CHECK(trident_indexed_read(&p,0,0,0)==TRI_E_INVAL);
    CHECK(trident_ar_read(&p,0,0,0)==TRI_E_INVAL);
    CHECK(trident_hidden_dac_read(&p,0)==TRI_E_INVAL);
    CHECK(trident_regs_bind(0)==TRI_E_INVAL);
    struct trident_device empty = {0};
    CHECK(trident_regs_bind(&empty)==TRI_E_INVAL);
    CHECK(trident_read8(&empty,0,0,&a)==TRI_E_INVAL);
    CHECK(trident_write8(&empty,0,0,0)==TRI_E_INVAL);
    CHECK(trident_port8(&empty,0,0)==TRI_E_INVAL);
    return 0;
}
static void emit(const char *s,u32 n)
{
    __asm__ volatile ("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
void _start(void)
{
    int rc=sequences(0x3D4);
    if (!rc) rc=sequences(0x3B4);
    if (!rc) rc=scalar_fake();
    if (!rc) rc=production();
    if (rc) {
        char msg[]="RED line 0000\n";
        u32 line=(u32)rc;
        for (int i=12; i>=9; --i) {msg[i]='0'+line%10; line/=10;}
        emit(msg,sizeof(msg)-1);
    } else emit("GREEN\n",6);
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(!!rc) : "memory");
    __builtin_unreachable();
}
