/* B0 only: link fifo.c and the A2 gf_command_host.h fixture/stubs.
 * A2 logs all writes as {bar,off,value}, including PRAMIN on BAR2.
 * init poisons PRAMIN with a5a5a5a5; set/get bypass the write log.
 * busy_reads applies when stored PULL0 bit0 is clear: -1 stays busy,
 * positive counts busy reads, then clears (set/get do not consume reads).
 * clock/wait failures and fault_rc are injected before the next callback. */
#include <assert.h>
#define GF2_HOST_B0
#include "gf_command_host.h"

static struct gf2_host h;
static unsigned pos;
static void expect(u32 bar, u32 off, u32 value)
{
    assert(pos < h.nwrite);
    assert(h.write[pos].bar == bar && h.write[pos].off == off);
    assert(h.write[pos++].value == value);
}
static void done(void) { assert(pos == h.nwrite); h.nwrite = pos = 0; }
static u32 fc(u32 off)
{
    u32 value;
    assert(gf_inst_read32(&h.f, &h.f.imem.ramfc, off, &value) == GF1_OK);
    return value;
}

static void fifo_layout(void)
{
    static const u32 init[][2] = {
        {0x2040,0xff}, {0x2044,0x2101ffff}, {0x2058,1}, {0x2210,0x03030100},
        {0x2218,0x180}, {0x2230,0}, /* 2220 checked separately */
        {0x3204,31}, {0x3200,1}, {0x3250,1}, {0x2500,1}
    };
    for (u32 v = 0x04000000; v <= 0x08000000; v *= 2) {
        for (u32 r = 0x200000; r <= 0x400000; r *= 2) {
            gf2_host_init(&h, v, r); pos = 0;
            h.ch.B = v - r - GF2_PUSH_BYTES; h.ch.push_dma.inst.off = 0x45670;
            assert(gf2_fb_init(&h.ch) == GF2_OK);
            expect(0,0x100850,0x80000000); expect(0,0x100800,1); done();
            assert(gf2_fifo_init(&h.ch) == GF2_OK);
            for (u32 off = 0; off < 128; off += 4) expect(2,0x20000+off,0);
            expect(2,0x20000,h.ch.B); expect(2,0x20004,h.ch.B);
            expect(2,0x2000c,0x4567); expect(2,0x20018,0x30086078);
            expect(2,0x2003c,0x1ffff);
            for (u32 i = 0; i < 6; ++i) expect(0,init[i][0],init[i][1]);
            expect(0,0x2220,((v-0x80000+0x20000)>>16)|0x30000);
            for (u32 i = 6; i < 10; ++i) expect(0,init[i][0],init[i][1]);
            done();
            for (u32 off = 0; off < 128; off += 4) {
                u32 value = off == 0 || off == 4 ? h.ch.B :
                            off == 0x0c ? 0x4567 : off == 0x18 ? 0x30086078 :
                            off == 0x3c ? 0x1ffff : 0;
                assert(fc(off) == value);
            }
            assert(fc(128) == 0xa5a5a5a5U); /* no adjacent channel write */
            for (unsigned active = 0; active < 2; ++active) {
                gf2_host_set(&h,0x3204,active ? 0x10000 : 31);
                gf2_host_set(&h,0x2500,0x81);
                assert(gf2_fifo_bind_gr(&h.ch,0x489a0) == GF2_OK);
                expect(0,0x2500,0x80);
                if (active) expect(0,0x32e0,0x489a);
                expect(2,0x20038,0x489a); expect(0,0x2500,0x81); done();
            }
            gf2_host_set(&h,0x2504,0x80);
            assert(gf2_fifo_start(&h.ch) == GF2_OK);
            expect(0,0x2504,0x81); done(); /* no other start writes */
        }
    }
}

static void fifo_stop(void)
{
    /* Independent oracle: all NV40 layout entries, including the split +18. */
    static const u32 layout[][3] = {
        {0x3240,0x00,0xffffffff}, {0x3244,0x04,0xffffffff},
        {0x3248,0x08,0xffffffff}, {0x322c,0x0c,0xffffffff},
        {0x32a0,0x10,0xffffffff}, {0x3228,0x14,0xffffffff},
        {0x3224,0x18,0x0fffffff}, {0x2058,0x18,0x30000000},
        {0x3280,0x1c,0xffffffff}, {0x3254,0x20,0xffffffff},
        {0x3268,0x24,0xffffffff}, {0x3264,0x28,0xffffffff},
        {0x3260,0x2c,0xffffffff}, {0x326c,0x30,0xffffffff},
        {0x324c,0x34,0xffffffff}, {0x32e0,0x38,0xffffffff},
        {0x2044,0x3c,0x1ffff}, {0x32e4,0x40,0xffffffff},
        {0x32e8,0x44,0xffffffff}, {0x2088,0x4c,0xffffffff},
        {0x3300,0x50,0xffffffff}, {0x330c,0x54,0xffffffff}
    };
    gf2_host_init(&h,0x04000000,0x200000); pos = 0;
    gf2_host_set(&h,0x3204,0x10000); gf2_host_set(&h,0x2504,0x81);
    gf2_host_set(&h,0x3220,0x81); gf2_host_set(&h,0x3250,0x81);
    for (unsigned i = 0; i < 22; ++i) gf2_host_set(&h,layout[i][0],0x5fffffffU-i);
    gf2_fifo_stop(&h.ch); assert(h.ch.last_rc == 0);
    expect(0,0x2500,0); expect(0,0x3220,0x80);
    expect(0,0x3200,0); expect(0,0x3250,0x80);
    u32 split = 0xa5a5a5a5;
    for (unsigned i = 0; i < 22; ++i) {
        u32 old = layout[i][1] == 0x18 ? split : 0xa5a5a5a5;
        u32 value = (old & ~layout[i][2]) | ((0x5fffffffU-i) & layout[i][2]);
        expect(2,0x20000+layout[i][1],value);
        if (layout[i][1] == 0x18) split = value;
        else assert(fc(layout[i][1]) == value);
    }
    assert(fc(0x18) == split);
    for (unsigned i = 0; i < 22; ++i) expect(0,layout[i][0],0);
    expect(0,0x3270,0); expect(0,0x3210,0); expect(0,0x3204,31);
    expect(0,0x3200,1); expect(0,0x3250,1);
    expect(0,0x2504,0x80); expect(0,0x2500,1); done();
    gf2_fifo_stop(&h.ch); /* inactive: no save/clear or puller writes */
    expect(0,0x2500,0); expect(0,0x2504,0x80); expect(0,0x2500,1); done();
}

static void fifo_pause(void)
{
    gf2_host_init(&h,0x04000000,0x200000); pos = 0;
    h.now = 0xfffffff0U; h.busy_reads = 3;
    gf2_host_set(&h,0x3250,0x81);
    assert(gf2_fifo_pause(&h.ch) == GF2_OK);
    expect(0,0x2500,0); expect(0,0x3250,0x80); expect(0,0x3258,0); done();
    gf2_fifo_resume(&h.ch); expect(0,0x3250,0x81); expect(0,0x2500,1); done();
    h.busy_reads = -1;
    gf2_host_set(&h,0x3250,0x81);
    assert(gf2_fifo_pause(&h.ch) == GF2_ETIME);
    expect(0,0x2500,0); expect(0,0x3250,0x80); done();
    h.busy_reads = 0; gf2_host_set(&h,0x3250,0x91);
    assert(gf2_fifo_pause(&h.ch) == GF2_EFAULT);
    expect(0,0x2500,0); expect(0,0x3250,0x90); done();
    h.clock_fail = true;
    assert(gf2_fifo_pause(&h.ch) == GF2_EIO);
    expect(0,0x2500,0); expect(0,0x3250,0x90); done();
    h.clock_fail = false; h.wait_fail = true; h.busy_reads = -1;
    gf2_host_set(&h,0x3250,0x81);
    assert(gf2_fifo_pause(&h.ch) == GF2_EIO);
    expect(0,0x2500,0); expect(0,0x3250,0x80); done();
    h.fault_rc = GF2_EFAULT;
    assert(gf2_fifo_start(&h.ch) == GF2_EFAULT); done();
    gf2_fifo_resume(&h.ch); assert(h.ch.last_rc == GF2_EFAULT); done();
}

int main(void) { fifo_layout(); fifo_stop(); fifo_pause(); return 0; }
