/* B2 packet contract: GF_FEAT3_DESIGN section 4 and G2. */
#define GF3_HOST_UNIT 2
#include "gf_accel_host.h"

static struct gf3_host host;
static struct gf3_host_event events[32];
static struct gf3_host_packet packets[8];
static u32 words[16];

static void packet(u32 n, u8 subc, u16 method, u16 count, const u32 *v)
{
    const struct gf3_host_packet *p = &host.packets[n];
    assert(p->subc == subc && p->method == method && p->count == count);
    assert(!p->noninc && p->deadline == 1234);
    for (u32 i = 0; i < count; ++i) assert(host.words[p->first+i] == v[i]);
}

/* GX truth table independently expands S,D into the hardware P,S,D index.
 * The three specified mask-independent operations ignore P. */
static u32 expected_rop(u32 alu, bool full)
{
    u32 result = 0;
    for (u32 i = 0; i < 8; ++i) {
        u32 s = (i >> 1) & 1, d = i & 1;
        u32 bit = (alu >> (((1-s) << 1) | (1-d))) & 1;
        if (!full && alu != 0 && alu != 10 && alu != 15 && !(i & 4)) bit = d;
        result |= bit << i;
    }
    return result;
}

static void exercise(enum gf3_fmt fmt, u32 bits, u32 pat, u32 alu, u32 mask)
{
    u32 fullmask = bits == 8 ? 0xffU : 0xffffU;
    u32 expanded = mask | ~fullmask;
    bool full = expanded == 0xffffffffU;
    u32 v[4];
    gf3_host_trace_clear(&host);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, (u8)alu, mask, fmt, 1234) == 0);
    assert(host.calls[H_FORMAT] == 1);
    if (alu == 3 && full) {
        assert(host.npackets == 1 && host.nwords == 1);
        v[0] = 3; packet(0, GF3_SUBC_GDI, 0x02fc, 1, v);
        return;
    }
    assert(host.npackets == 6 && host.nwords == 9);
    v[0] = GF3_HANDLE_PATTERN; packet(0, GF3_SUBC_MISC, 0, 1, v);
    v[0] = pat; packet(1, GF3_SUBC_MISC, 0x0300, 1, v);
    v[0] = full ? 0xffffffffU : 0; v[1] = expanded;
    v[2] = v[3] = 0xffffffffU; packet(2, GF3_SUBC_MISC, 0x0310, 4, v);
    v[0] = GF3_HANDLE_ROP; packet(3, GF3_SUBC_MISC, 0, 1, v);
    v[0] = expected_rop(alu, full); packet(4, GF3_SUBC_MISC, 0x0300, 1, v);
    v[0] = 1; packet(5, GF3_SUBC_GDI, 0x02fc, 1, v);
}

void _start(void)
{
    gf3_host_init(&host);
    host.events = events; host.cevents = sizeof(events)/sizeof(events[0]);
    host.packets = packets; host.cpackets = sizeof(packets)/sizeof(packets[0]);
    host.words = words; host.cwords = sizeof(words)/sizeof(words[0]);
    for (u32 alu = 0; alu < 16; ++alu) {
        exercise(GF3_INDEX8, 8, 3, alu, 0);
        exercise(GF3_INDEX8, 8, 3, alu, 0xff);
        exercise(GF3_INDEX8, 8, 3, alu, 0x55);
        exercise(GF3_Y8, 8, 3, alu, 0x55);
        exercise(GF3_RGB565, 16, 1, alu, 0);
        exercise(GF3_RGB565, 16, 1, alu, 0xffff);
        exercise(GF3_RGB565, 16, 1, alu, 0x5555);
    }
    gf3_host_trace_clear(&host);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_BLIT, 3, 0xffffffffU, GF3_XRGB8888, 1234) == 0);
    u32 v = 3; packet(0, GF3_SUBC_BLIT, 0x02fc, 1, &v);
    gf3_host_trace_clear(&host);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_BLIT, 3, 0xffffffffU, GF3_ARGB8888, 1234) == 0);
    packet(0, GF3_SUBC_BLIT, 0x02fc, 1, &v);
    gf3_host_trace_clear(&host);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, 6, 0xffffffffU, GF3_XRGB8888, 1234) == GF3_EUNSUPPORTED);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, 3, 0xfffffffeU, GF3_ARGB8888, 1234) == GF3_EUNSUPPORTED);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, 3, 0, GF3_YUY2, 1234) == GF3_EFORMAT);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, 16, 0, GF3_INDEX8, 1234) == GF3_EINVAL);
    assert(host.npackets == 0);
    for (u32 nth = 1; nth <= 6; ++nth) {
        gf3_host_trace_clear(&host);
        gf3_host_fail_at(&host, H_EMIT, nth, GF2_EIO);
        assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, 6, 0x55, GF3_INDEX8, 1234) == GF2_EIO);
        assert(host.ctx.last_rc == GF2_EIO && host.calls[H_EMIT] == nth && host.npackets == nth-1);
    }
    gf3_host_fail_at(&host, H_EMIT, 0, 0);
    gf3_host_trace_clear(&host);
    gf3_host_fail_at(&host, H_FORMAT, 1, GF3_EFORMAT);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, 3, 0, GF3_INDEX8, 1234) == GF3_EFORMAT);
    assert(host.npackets == 0);
    gf2_host_exit(0);
}
