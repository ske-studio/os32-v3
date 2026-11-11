#define GF3_HOST_UNIT 3
#include "gf_accel_host.h"

static struct gf3_host host;
static struct gf3_host_event events[64];
static struct gf3_host_packet packets[8];
static u32 words[16];
static struct gf_vram_reservation_node reservation;
static const struct gf3_surface surface = {0x1000, 4096, 64, 64, 64, GF3_INDEX8, 1, 1, 0};
static const struct gf3_rect rect = {7, 11, 13, 17};
#define DEADLINE 500U
/* Independent method and format expectations: nv04_exa.c:118-129,153-157;
 * format rows: GF_FEAT3_DESIGN.md section 9, nv04_exa.c:97-129. */
#define EXPECT_GDI_FORMAT_METHOD 0x0300U
#define EXPECT_GDI_COLOR_METHOD  0x03fcU
#define EXPECT_GDI_RECT_METHOD   0x0400U
#define EXPECT_GDI_INDEX8         3U
#define EXPECT_GDI_RGB565         1U
#define EXPECT_GDI_ARGB8888       3U
#define TEST_COLOR 0x12345678U
#define GX_COPY 3U
#define GX_XOR  6U

static void setup(void)
{
    gf3_host_init(&host);
    host.events = events; host.cevents = 64;
    host.packets = packets; host.cpackets = 8;
    host.words = words; host.cwords = 16;
    reservation.value = (struct gf_vram_reservation){&host, 0x1000, 4096, 1, 1};
    reservation.next = 0; host.reservations = &reservation;
}
static int fill(void)
{
    return gf3_fill(&host.ctx, &surface, rect, TEST_COLOR, GX_COPY, 0xff, DEADLINE);
}
static void packet(u32 i, u16 method, u16 count, u32 first, u32 second)
{
    const struct gf3_host_packet *p = &packets[i];
    assert(p->subc == GF3_SUBC_GDI && p->method == method && p->count == count);
    assert(!p->noninc && p->deadline == DEADLINE);
    assert(words[p->first] == first);
    if (count == 2) assert(words[p->first + 1] == second);
}
void _start(void)
{
    setup();
    assert(!gf3_fill_emit(&host.ctx, &surface, rect, TEST_COLOR, GX_COPY, 0xff, DEADLINE));
    assert(host.npackets == 3 && !host.calls[H_FENCE] && !host.calls[H_WAIT]);
    setup();
    assert(!gf3_fill_emit(&host.ctx, &surface, rect, TEST_COLOR, GF3_ALU_KEEP, 0, DEADLINE));
    assert(!host.calls[H_ROP] && host.npackets == 3);
    for (u8 alu = 0; alu < 16; ++alu) {
        setup();
        assert(!gf3_fill_emit(&host.ctx, &surface, rect, TEST_COLOR, alu, 0xff, DEADLINE));
        assert(host.calls[H_ROP] == 1);
    }
    setup();
    assert(fill() == 0 && host.ctx.last_rc == 0);
    assert(host.npackets == 3 && host.nwords == 4);
    packet(0, EXPECT_GDI_FORMAT_METHOD, 1, EXPECT_GDI_INDEX8, 0);
    packet(1, EXPECT_GDI_COLOR_METHOD, 1, TEST_COLOR, 0);
    /* GDI packs x/width in the high half, y/height in the low half:
     * nv04_exa.c:153-157. Derive expected words from the input rectangle. */
    packet(2, EXPECT_GDI_RECT_METHOD, 2, (rect.x << 16) | rect.y,
           (rect.w << 16) | rect.h);
    assert(events[1].op == H_SURFACE_CHECK && events[1].a[4] == GF3_USE_GDI);
    assert(events[2].op == H_ROP && events[2].a[0] == GF3_SUBC_GDI &&
           events[2].a[1] == 3 && events[2].a[2] == 0xff &&
           events[2].a[3] == GF3_INDEX8 && events[2].a[4] == DEADLINE);
    assert(events[3].op == H_SURFACE_SET && events[3].p[1] == &surface &&
           events[3].p[2] == &surface && events[3].a[0] == 1 && events[3].a[1] == DEADLINE);
    assert(host.calls[H_FENCE] == 1 && host.calls[H_WAIT] == 1);
    assert(events[7].op == H_FENCE && events[7].a[0] == DEADLINE);
    assert(events[8].op == H_WAIT && events[8].a[2] == DEADLINE);

    /* The ROP dependency is stubbed: check its inputs independently of the
     * emitted packets. 32-bit uses the supported copy/full-mask path. */
    const struct {
        enum gf3_fmt fmt;
        u32 cpp, expected_gdi, mask;
        u8 alu;
    } cases[] = {
        {GF3_RGB565, 2, EXPECT_GDI_RGB565, 0x55aaU, GX_XOR},
        {GF3_ARGB8888, 4, EXPECT_GDI_ARGB8888, 0xffffffffU, GX_COPY},
    };
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        struct gf3_surface s = surface;
        setup();
        s.fmt = cases[i].fmt;
        s.pitch = s.width * cases[i].cpp;
        s.bytes = s.pitch * s.height;
        reservation.value.bytes = s.bytes;
        assert(gf3_fill(&host.ctx, &s, rect, TEST_COLOR, cases[i].alu,
                        cases[i].mask, DEADLINE) == 0);
        assert(host.npackets == 3 && host.nwords == 4);
        packet(0, EXPECT_GDI_FORMAT_METHOD, 1, cases[i].expected_gdi, 0);
        packet(1, EXPECT_GDI_COLOR_METHOD, 1, TEST_COLOR, 0);
        packet(2, EXPECT_GDI_RECT_METHOD, 2, (rect.x << 16) | rect.y,
               (rect.w << 16) | rect.h);
        assert(events[2].op == H_ROP && events[2].a[0] == GF3_SUBC_GDI &&
               events[2].a[1] == cases[i].alu && events[2].a[2] == cases[i].mask &&
               events[2].a[3] == cases[i].fmt && events[2].a[4] == DEADLINE);
    }

    const enum gf3_host_op failures[] = {H_FAULT, H_SURFACE_CHECK, H_ROP, H_SURFACE_SET, H_EMIT, H_FENCE, H_WAIT};
    for (u32 i = 0; i < sizeof(failures)/sizeof(failures[0]); ++i) {
        u32 count = failures[i] == H_EMIT ? 3 : 1;
        for (u32 n = 1; n <= count; ++n) {
            setup(); gf3_host_fail_at(&host, failures[i], n, GF2_EIO);
            assert(fill() == GF2_EIO && host.ctx.last_rc == GF2_EIO);
            assert(host.calls[failures[i]] == n);
            if (failures[i] == H_SURFACE_CHECK || failures[i] == H_ROP) assert(!host.npackets);
            if (failures[i] != H_WAIT) assert(!host.calls[H_WAIT]);
        }
    }
    setup();
    assert(gf3_fill(&host.ctx, &surface, (struct gf3_rect){0,0,0,1}, 0, 0, 0, DEADLINE) == 0);
    assert(!host.calls[H_SURFACE_CHECK] && !host.npackets && !host.calls[H_FENCE]);
    host.ctx.ready = false;
    assert(fill() == GF3_ESTATE && !host.npackets);
    host.ctx.ready = true; ++host.ctx.epoch;
    assert(fill() == GF3_ESTATE && !host.npackets);
    host.ctx.epoch = host.ch.epoch; host.ch.state = GF2_OFF;
    assert(fill() == GF3_ESTATE && !host.npackets);
    setup();
    assert(gf3_fill(&host.ctx, 0, rect, 0, 3, 0xff, DEADLINE) == GF3_EINVAL);
    assert(gf3_fill(0, &surface, rect, 0, 3, 0xff, DEADLINE) == GF3_EINVAL);
    assert(gf3_fill(&host.ctx, &surface, (struct gf3_rect){63,0,2,1}, 0, 3, 0xff, DEADLINE) == GF3_ERANGE);
    assert(!host.npackets);
    gf2_host_exit(0);
}
