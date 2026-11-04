/* B2 packet contract: GF_FEAT3_DESIGN section 4 and G2. */
#define GF3_HOST_UNIT 2
#include "gf_accel_host.h"

static struct gf3_host host;
static struct gf3_host_event events[32];
static struct gf3_host_packet packets[8];
static u32 words[16];

/* xf86-video-nouveau/src/hwdefs/nv01_2d.xml.h:118,136-140,216,683-687;
 * nv04_exa.c:50-81 (object binding and operation selection). */
#define MTHD_OBJECT 0x0000U
#define MTHD_OPERATION 0x02fcU
#define MTHD_PATTERN_FORMAT 0x0300U
#define MTHD_PATTERN_DATA 0x0310U
#define MTHD_ROP 0x0300U
#define OPERATION_ROP 1U
#define OPERATION_COPY 3U
/* nv01_2d.xml.h:119-121: pattern color formats. */
#define PATTERN_RGB565 1U
#define PATTERN_INDEX8 3U
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* nv04_exa.c:50-81: words in each emitted packet. */
static const u16 packet_words[] = {1, 1, 4, 1, 1, 1};

static void packet(u32 n, u8 subc, u16 method, u16 count, const u32 *v)
{
    const struct gf3_host_packet *p = &host.packets[n];
    assert(p->subc == subc && p->method == method && p->count == count);
    assert(!p->noninc && p->deadline == 1234);
    for (u32 i = 0; i < count; ++i) assert(host.words[p->first+i] == v[i]);
}

/* Independently resolved from NVROP copy/copy_planemask and ROP definitions:
 * xf86-video-nouveau/src/nv_rop.h:303-318, 32-287. */
static u32 expected_rop(u32 alu, bool full)
{
    static const u8 expected[16][2] = {
        {0x00, 0x00}, {0x88, 0x8a}, {0x44, 0x45}, {0xcc, 0xca},
        {0x22, 0x2a}, {0xaa, 0xaa}, {0x66, 0x6a}, {0xee, 0xea},
        {0x11, 0x15}, {0x99, 0x95}, {0x55, 0x55}, {0xdd, 0xd5},
        {0x33, 0x2e}, {0xbb, 0xba}, {0x77, 0x75}, {0xff, 0xff}
    };
    return expected[alu][!full];
}

static void exercise(enum gf3_fmt fmt, u32 bits, u32 pat, u32 alu, u32 mask)
{
    u32 fullmask = bits == 8 ? 0xffU : 0xffffU;
    u32 expanded = mask | ~fullmask;
    bool full = expanded == 0xffffffffU;
    u32 v[4];
    u32 total_words = 0;
    for (u32 i = 0; i < ARRAY_COUNT(packet_words); ++i) total_words += packet_words[i];
    gf3_host_trace_clear(&host);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, (u8)alu, mask, fmt, 1234) == 0);
    assert(host.calls[H_FORMAT] == 1);
    if (alu == 3 && full) {
        assert(host.npackets == 1 && host.nwords == packet_words[0]);
        v[0] = OPERATION_COPY; packet(0, GF3_SUBC_GDI, MTHD_OPERATION, packet_words[0], v);
        return;
    }
    assert(host.npackets == ARRAY_COUNT(packet_words) && host.nwords == total_words);
    v[0] = GF3_HANDLE_PATTERN; packet(0, GF3_SUBC_MISC, MTHD_OBJECT, packet_words[0], v);
    v[0] = pat; packet(1, GF3_SUBC_MISC, MTHD_PATTERN_FORMAT, packet_words[1], v);
    v[0] = full ? 0xffffffffU : 0; v[1] = expanded;
    v[2] = v[3] = 0xffffffffU; packet(2, GF3_SUBC_MISC, MTHD_PATTERN_DATA, packet_words[2], v);
    v[0] = GF3_HANDLE_ROP; packet(3, GF3_SUBC_MISC, MTHD_OBJECT, packet_words[3], v);
    v[0] = expected_rop(alu, full); packet(4, GF3_SUBC_MISC, MTHD_ROP, packet_words[4], v);
    v[0] = OPERATION_ROP; packet(5, GF3_SUBC_GDI, MTHD_OPERATION, packet_words[5], v);
}

void _start(void)
{
    gf3_host_init(&host);
    host.events = events; host.cevents = sizeof(events)/sizeof(events[0]);
    host.packets = packets; host.cpackets = sizeof(packets)/sizeof(packets[0]);
    host.words = words; host.cwords = sizeof(words)/sizeof(words[0]);
    for (u32 alu = 0; alu < 16; ++alu) {
        exercise(GF3_INDEX8, 8, PATTERN_INDEX8, alu, 0);
        exercise(GF3_INDEX8, 8, PATTERN_INDEX8, alu, 0xff);
        exercise(GF3_INDEX8, 8, PATTERN_INDEX8, alu, 0x55);
        exercise(GF3_Y8, 8, PATTERN_INDEX8, alu, 0x55);
        exercise(GF3_RGB565, 16, PATTERN_RGB565, alu, 0);
        exercise(GF3_RGB565, 16, PATTERN_RGB565, alu, 0xffff);
        exercise(GF3_RGB565, 16, PATTERN_RGB565, alu, 0x5555);
    }
    /* Target clear's partial-mask ROP and 16-bit mask upper expansion. */
    exercise(GF3_INDEX8, 8, PATTERN_INDEX8, 0, 0x0f);
    exercise(GF3_RGB565, 16, PATTERN_RGB565, 3, 0x5555);
    gf3_host_trace_clear(&host);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_BLIT, 3, 0xffffffffU, GF3_XRGB8888, 1234) == 0);
    u32 v = OPERATION_COPY; packet(0, GF3_SUBC_BLIT, MTHD_OPERATION, 1, &v);
    gf3_host_trace_clear(&host);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_BLIT, 3, 0xffffffffU, GF3_ARGB8888, 1234) == 0);
    packet(0, GF3_SUBC_BLIT, MTHD_OPERATION, 1, &v);
    gf3_host_trace_clear(&host);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, 6, 0xffffffffU, GF3_XRGB8888, 1234) == GF3_EUNSUPPORTED);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, 3, 0xfffffffeU, GF3_ARGB8888, 1234) == GF3_EUNSUPPORTED);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, 3, 0, GF3_YUY2, 1234) == GF3_EFORMAT);
    assert(gf3_rop_set(&host.ctx, GF3_SUBC_GDI, 16, 0, GF3_INDEX8, 1234) == GF3_EINVAL);
    assert(host.npackets == 0);
    for (u32 nth = 1; nth <= ARRAY_COUNT(packet_words); ++nth) {
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
