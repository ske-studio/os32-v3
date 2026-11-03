/* B6 packet oracle: xf86-video-nouveau/src/nv04_exa.c:312-374 (MIT),
 * GF_FEAT3_DESIGN section 7. Literal method/payload expectations are
 * intentionally independent of accel_ifc.c's aliases and packing loops.
 * Link this TU with hwdev/gf/accel_ifc.c, -m32 -nostdlib, no libc/CRT. */
#define GF3_HOST_UNIT 6
#include "gf_accel_host.h"

/* Capacity for the 1024-row case: three setup packets, two per row;
 * each row contributes three geometry words and two pixel words. */
#define TEST_ROWS 1024U
#define TRACE_PACKETS (3U + 2U * TEST_ROWS)
#define TRACE_WORDS (5U + 5U * TEST_ROWS + 1792U)
#define TRACE_EVENTS (2U * TRACE_PACKETS + 32U)
#define TEST_DEADLINE 200U
#define SOURCE_BYTES (TEST_ROWS * 11U + 8U)
#define GUARD_PAGE_BYTES 4096U
static struct gf3_host host;
static struct gf3_host_packet packets[TRACE_PACKETS];
static struct gf3_host_event events[TRACE_EVENTS];
static u32 words[TRACE_WORDS];
static u8 source[SOURCE_BYTES];
static u8 guarded[2U * GUARD_PAGE_BYTES] __attribute__((aligned(GUARD_PAGE_BYTES)));
static struct gf_vram_reservation_node reservation;
static struct gf3_surface surface;
static struct gf3_ram ram;
static struct gf3_rect rect;

static void observe(struct gf3_host *h, const struct gf3_host_event *e)
{
    if (e->op == H_EMIT) {
        assert(h->calls[H_SURFACE_CHECK] >= 2 && h->calls[H_RAM_CHECK] == 1);
        assert(e->a[4] == TEST_DEADLINE);
    } else if (e->op == H_SURFACE_CHECK) {
        assert(e->p[0] == &h->ctx && e->p[1] == &surface);
        if (e->nth == 1) {
            assert(e->a[0] == rect.x && e->a[1] == rect.y);
            assert(e->a[2] == rect.w && e->a[3] == rect.h);
            assert(e->a[4] == GF3_USE_IFC);
        } else {
            assert(e->nth == 2 && e->a[0] == 0 && e->a[1] == 0);
            assert(e->a[2] == surface.width && e->a[3] == surface.height);
            assert(e->a[4] == GF3_USE_2D);
        }
    } else if (e->op == H_RAM_CHECK) {
        assert(e->p[0] == &ram && e->a[0] == rect.w * gf3_host_cpp(surface.fmt));
        assert(e->a[1] == rect.h);
    } else if (e->op == H_SURFACE_SET) {
        assert(e->p[0] == &h->ctx && e->p[1] == &surface && e->p[2] == &surface);
        assert(e->a[0] == false && e->a[1] == TEST_DEADLINE);
        assert(h->npackets == 2);
    } else if (e->op == H_FENCE) {
        assert(e->a[0] == TEST_DEADLINE && h->npackets == 3U + 2U * rect.h);
    } else if (e->op == H_WAIT) {
        assert(e->a[0] == h->ch.epoch && e->a[1] == h->ch.seq);
        assert(e->a[2] == TEST_DEADLINE && h->calls[H_FENCE] == 1);
    }
}

static void setup(enum gf3_fmt fmt, u32 w, u32 h, u32 pitch)
{
    gf3_host_init(&host);
    host.packets = packets; host.cpackets = TRACE_PACKETS;
    host.words = words; host.cwords = TRACE_WORDS;
    host.events = events; host.cevents = TRACE_EVENTS;
    host.observe = observe;
    u32 cpp = gf3_host_cpp(fmt);
    rect = (struct gf3_rect){3, 5, w, h};
    surface = (struct gf3_surface){
        .off = 4096, .pitch = (w + rect.x) * cpp + 3U,
        .width = w + rect.x, .height = h + rect.y, .fmt = fmt,
        .reservation_id = 9, .generation = 2
    };
    surface.bytes = surface.pitch * surface.height;
    reservation = (struct gf_vram_reservation_node){
        .value = {&host, surface.off, surface.bytes, 9, 2}
    };
    host.reservations = &reservation;
    ram = (struct gf3_ram){source + 1, (h - 1U) * pitch + w * cpp, pitch};
    assert(ram.bytes < sizeof(source));
    for (u32 i = 0; i < sizeof(source); ++i) source[i] = (u8)(i * 37U + 11U);
    /* Nonzero scratch makes omitted padding observable. */
    gf2_host_memset(host.ctx.scratch, 0xa5, sizeof(host.ctx.scratch));
}

static int run(void)
{
    int rc = gf3_ifc(&host.ctx, &surface, rect, &ram, TEST_DEADLINE);
    assert(host.ctx.last_rc == rc);
    return rc;
}

static const u32 *packet(u32 index, u8 subc, u16 method, u16 count)
{
    assert(index < host.npackets);
    const struct gf3_host_packet *p = &host.packets[index];
    assert(p->subc == subc && p->method == method && p->count == count);
    assert(!p->noninc && p->deadline == TEST_DEADLINE);
    return host.words + p->first;
}

static void check_packets(u32 cpp, u32 ifc_format)
{
    assert(host.npackets == 3U + 2U * rect.h);
    const u32 *v = packet(0, 6, 0x0000, 1);
    assert(v[0] == 0x80000109U);
    v = packet(1, 6, 0x0300, 2);
    assert(v[0] == (rect.y * 65536U + rect.x));
    assert(v[1] == (rect.h * 65536U + rect.w));
    v = packet(2, 5, 0x02fc, 2);
    assert(v[0] == 3 && v[1] == ifc_format);
    u32 rowbytes = rect.w * cpp;
    u32 padded = ((rowbytes + 7U) / 8U) * 8U;
    for (u32 y = 0; y < rect.h; ++y) {
        v = packet(3U + y * 2U, 5, 0x0304, 3);
        assert(v[0] == ((rect.y + y) * 65536U + rect.x));
        assert(v[1] == 65536U + rect.w);
        assert(v[2] == 65536U + padded / cpp);
        v = packet(4U + y * 2U, 5, 0x0400, (u16)(padded / 4U));
        /* Compare every emitted byte, including zero padding and every
         * row's arbitrary pitch; no GPU emulator or product helper. */
        for (u32 b = 0; b < padded; ++b) {
            u8 actual = (u8)(v[b / 4U] >> (8U * (b % 4U)));
            u8 expected = b < rowbytes ? ((u8 *)ram.ptr)[y * ram.pitch + b] : 0;
            assert(actual == expected);
        }
    }
    assert(host.calls[H_SURFACE_SET] == 1);
    assert(host.calls[H_FENCE] == 1 && host.calls[H_WAIT] == 1);
}

static void padding_rows(void)
{
    const enum gf3_fmt formats[] = {GF3_RGB565, GF3_XRGB8888, GF3_ARGB8888};
    for (u32 f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f) {
        u32 cpp = f ? 4U : 2U;
        for (u32 width = f ? 1U : 2U; width <= 9; ++width) {
            setup(formats[f], width, 3, width * cpp + 5U);
            assert(run() == 0);
            check_packets(cpp, f ? 4U : 1U);
        }
    }
    setup(GF3_RGB565, 2, TEST_ROWS, 11);
    assert(run() == 0);
    check_packets(2, 1);
    /* Maximum 1792 words ends the incrementing packet at method 0x1ffc. */
    setup(GF3_XRGB8888, 1791, 1, 1791U * 4U);
    assert(run() == 0);
    check_packets(4, 4);
    setup(GF3_XRGB8888, 1792, 1, 1792U * 4U);
    assert(run() == 0);
    check_packets(4, 4);
}

static void no_submit(int expected)
{
    assert(run() == expected);
    assert(host.calls[H_EMIT] == 0 && host.calls[H_SURFACE_SET] == 0);
    assert(host.calls[H_FENCE] == 0 && host.calls[H_WAIT] == 0);
}

static void boundaries(void)
{
    setup(GF3_RGB565, 1, 1, 2); no_submit(GF3_EUNSUPPORTED);
    setup(GF3_RGB565, 2, 1025, 4); no_submit(GF3_EUNSUPPORTED);
    setup(GF3_RGB565, 3585, 1, 7170); no_submit(GF3_EUNSUPPORTED);
    setup(GF3_XRGB8888, 1793, 1, 7172); no_submit(GF3_EUNSUPPORTED);
    const enum gf3_fmt rejected[] = {GF3_INDEX8, GF3_Y8, GF3_YUY2, GF3_UYVY};
    for (u32 i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        setup(rejected[i], 4, 1, 16); no_submit(GF3_EFORMAT);
    }
    setup(GF3_RGB565, 3, 2, 9); --ram.bytes; no_submit(GF3_ERANGE);
    setup(GF3_RGB565, 3, 2, 9); ram.pitch = 5; no_submit(GF3_ERANGE);
    setup(GF3_RGB565, 3, 2, 9); ram.ptr = 0; no_submit(GF3_EINVAL);
    setup(GF3_RGB565, 3, 2, 9); ++rect.w; no_submit(GF3_ERANGE);
    setup(GF3_RGB565, 3, 2, 9); ++surface.generation; no_submit(GF3_ESTATE);
    setup(GF3_RGB565, 3, 2, 9); surface.fmt = (enum gf3_fmt)99; no_submit(GF3_EFORMAT);
    setup(GF3_RGB565, 3, 2, 9); surface.pitch = 65536; no_submit(GF3_ERANGE);
    setup(GF3_RGB565, 3, 2, 9);
    surface.width = 65536; surface.pitch = surface.width * 2U;
    surface.bytes = surface.pitch * surface.height;
    reservation.value.bytes = surface.bytes;
    no_submit(GF3_ERANGE);
    /* Full SF setup also packs its dimensions: reject before CLIP. */
    setup(GF3_RGB565, 3, 2, 9);
    surface.height = 65536; surface.bytes = surface.pitch * surface.height;
    reservation.value.bytes = surface.bytes;
    no_submit(GF3_ERANGE);
}

static void state_and_zero(void)
{
    setup(GF3_RGB565, 3, 2, 9);
    assert(gf3_ifc(0, &surface, rect, &ram, TEST_DEADLINE) == GF3_EINVAL);
    assert(gf3_ifc(&host.ctx, 0, rect, &ram, TEST_DEADLINE) == GF3_EINVAL);
    assert(host.ctx.last_rc == GF3_EINVAL);
    assert(gf3_ifc(&host.ctx, &surface, rect, 0, TEST_DEADLINE) == GF3_EINVAL);
    host.ctx.ch = 0; no_submit(GF3_EINVAL);
    setup(GF3_RGB565, 3, 2, 9); host.ctx.ready = false; no_submit(GF3_ESTATE);
    setup(GF3_RGB565, 3, 2, 9); ++host.ch.epoch; no_submit(GF3_ESTATE);
    setup(GF3_RGB565, 3, 2, 9); host.ch.state = GF2_OFF; no_submit(GF3_ESTATE);
    setup(GF3_RGB565, 3, 2, 9); host.ch.fault.latched = true; no_submit(GF2_EFAULT);
    for (u32 axis = 0; axis < 2; ++axis) {
        setup(GF3_RGB565, 3, 2, 9);
        /* Zero requests do not inspect descriptor contents or RAM.ptr. */
        surface = (struct gf3_surface){0}; ram = (struct gf3_ram){0};
        if (axis) rect.h = 0; else rect.w = 0;
        host.ctx.last_rc = GF2_EIO;
        no_submit(0);
        assert(!host.calls[H_SURFACE_CHECK] && !host.calls[H_RAM_CHECK]);
        host.ctx.ready = false; no_submit(GF3_ESTATE);
    }
}

static void failures(void)
{
    const enum gf3_host_op ops[] = {
        H_FAULT, H_SURFACE_CHECK, H_RAM_CHECK, H_SURFACE_SET, H_FENCE, H_WAIT
    };
    for (u32 i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        setup(GF3_RGB565, 3, 2, 9);
        gf3_host_fail_at(&host, ops[i], 1, GF2_EIO);
        assert(run() == GF2_EIO);
        assert(host.calls[ops[i]] == 1);
        if (ops[i] == H_FAULT || ops[i] == H_SURFACE_CHECK || ops[i] == H_RAM_CHECK)
            assert(!host.calls[H_EMIT]);
        if (ops[i] != H_WAIT) assert(!host.calls[H_WAIT]);
    }
    setup(GF3_RGB565, 3, 2, 9);
    gf3_host_fail_at(&host, H_SURFACE_CHECK, 2, GF2_EMAP);
    no_submit(GF2_EMAP);
    /* Two-row trace: 3 setup emits and 2 emits per row. Fail every emit,
     * including a partially submitted row; never retry or add a fence. */
    for (u32 nth = 1; nth <= 3U + 2U * 2U; ++nth) {
        setup(GF3_RGB565, 3, 2, 9);
        gf3_host_fail_at(&host, H_EMIT, nth, GF2_ENOSPC);
        assert(run() == GF2_ENOSPC);
        assert(host.calls[H_EMIT] == nth && host.npackets == nth - 1U);
        assert(!host.calls[H_FENCE] && !host.calls[H_WAIT]);
    }
    setup(GF3_RGB565, 3, 2, 9);
    host.now = TEST_DEADLINE - 1U; host.complete_after = 2;
    assert(run() == GF2_ETIME);
    assert(host.waits == 1 && host.calls[H_FENCE] == 1 && host.calls[H_WAIT] == 1);
    setup(GF3_RGB565, 3, 2, 9);
    host.complete_after = 3;
    assert(run() == 0 && host.waits == 3);
    check_packets(2, 1);
    assert(!host.calls[H_IFC] && !host.calls[H_M2MF] && !host.calls[H_UPLOAD]);
}

static void guard_tail(void)
{
    /* Linux/i386 mprotect(125): a missing byte may not hide in accessible
     * RAM after the final row. No libc and no allocation required. */
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc)
                     : "0"(125), "b"(guarded + GUARD_PAGE_BYTES),
                       "c"(GUARD_PAGE_BYTES), "d"(0) : "memory", "cc");
    assert(rc == 0);
    setup(GF3_RGB565, 3, 2, 9);
    u8 *tail = guarded + GUARD_PAGE_BYTES - ram.bytes;
    gf2_host_memcpy(tail, ram.ptr, ram.bytes);
    ram.ptr = tail;
    assert(run() == 0);
    check_packets(2, 1);
}

void _start(void)
{
    padding_rows();
    boundaries();
    state_and_zero();
    failures();
    guard_tail();
    gf2_host_exit(0);
}
