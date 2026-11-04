/* Link accel_mono.c only; dependencies come from A1 UNIT 5.
 * Packet oracle: MIT nv04_fbcon.c:105-124,250 / design section 6.
 * No GPU image emulation: compare payloads and dependency arguments. */
#define GF3_HOST_UNIT 5
#include "gf_accel_host.h"

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define DEADLINE 123456U
#define INPUT_PITCH 83U /* 640 bits plus deliberately nonzero row gaps. */
static struct gf3_host host;
static struct gf3_host_event events[256];
static struct gf3_host_packet packets[128];
static u32 words[10000]; /* 640*480/32 + setup and format packets. */
static u8 input[INPUT_PITCH * 480];
static struct gf_vram_reservation_node reservation;
static struct gf3_surface surface;
static struct gf3_ram bits;
static struct gf3_rect rect, clipping;
static const u32 fg = 0x12345678U, bg = 0xabcdef01U;

static void observe(struct gf3_host *h, const struct gf3_host_event *e)
{
    assert(h == &host);
    if (e->op == H_SURFACE_CHECK) {
        assert(e->p[0] == &h->ctx && e->p[1] == &surface);
        assert(e->a[0] == rect.x && e->a[1] == rect.y &&
               e->a[2] == rect.w && e->a[3] == rect.h && e->a[4] == GF3_USE_GDI);
    }
    if (e->op == H_RAM_CHECK) {
        assert(e->p[0] == &bits && e->a[0] == (rect.w+7)/8 && e->a[1] == rect.h);
    }
    if (e->op == H_SURFACE_SET) {
        assert(e->p[0] == &h->ctx && e->p[1] == &surface && e->p[2] == &surface);
        assert(e->a[0] == true && e->a[1] == DEADLINE);
        assert(h->calls[H_SURFACE_CHECK] && h->calls[H_RAM_CHECK] && !h->calls[H_EMIT]);
    }
    if (e->op == H_FENCE) assert(e->a[0] == DEADLINE);
    if (e->op == H_WAIT) assert(e->a[2] == DEADLINE && e->a[0] == h->ch.epoch);
}
static void reset(void)
{
    gf3_host_init(&host);
    host.events = events; host.cevents = ARRAY_COUNT(events);
    host.packets = packets; host.cpackets = ARRAY_COUNT(packets);
    host.words = words; host.cwords = ARRAY_COUNT(words); host.observe = observe;
    surface = (struct gf3_surface){.off=4096, .bytes=640*480, .pitch=640,
        .width=640, .height=480, .fmt=GF3_INDEX8, .reservation_id=1, .generation=1};
    reservation = (struct gf_vram_reservation_node){
        .value={&host, surface.off, surface.bytes, 1, 1}};
    host.reservations = &reservation;
    rect = (struct gf3_rect){2, 3, 17, 5};
    clipping = (struct gf3_rect){4, 4, 11, 3};
    bits = (struct gf3_ram){input, sizeof(input), INPUT_PITCH};
    for (u32 i = 0; i < sizeof(input); ++i) input[i] = (u8)(i*37U+0x81U);
}
static int draw(void)
{
    int rc = gf3_mono(&host.ctx, &surface, rect, &bits, fg, bg, clipping, DEADLINE);
    assert(host.ctx.last_rc == rc);
    return rc;
}
static void packet(u32 index, u16 method, u16 count, const u32 *expected)
{
    assert(index < host.npackets);
    const struct gf3_host_packet *p = &packets[index];
    assert(p->method == method && p->count == count && p->subc == 3 && !p->noninc);
    assert(p->deadline == DEADLINE);
    if (expected) for (u32 i = 0; i < count; ++i) assert(words[p->first+i] == expected[i]);
}
static void check_stream(u32 left_top, u32 right_bottom)
{
    const u32 operation[] = {3}, format[] = {1};
    const u32 setup[] = {left_top, right_bottom, bg, fg,
        (rect.h << 16) | ((rect.w+7)&~7U), (rect.h << 16) | rect.w,
        (rect.y << 16) | rect.x};
    packet(0, 0x02fc, 1, operation); packet(1, 0x0304, 1, format);
    packet(2, 0x0be4, ARRAY_COUNT(setup), setup);
    u32 rowbytes = (rect.w+7)/8, total = rowbytes*rect.h, offset = 0;
    for (u32 i = 3; i < host.npackets; ++i) {
        u32 remaining = (total+3)/4-offset/4;
        packet(i, 0x0c00, remaining > 128 ? 128 : remaining, 0);
        for (u32 j = 0; j < packets[i].count*sizeof(u32); ++j, ++offset) {
            u8 expected = offset < total ? ((u8 *)bits.ptr)[offset/rowbytes*bits.pitch+offset%rowbytes] : 0;
            u8 actual = (u8)(words[packets[i].first+j/4] >> ((j%4)*8));
            assert(actual == expected);
        }
    }
    assert(offset == ((total+3)&~3U));
    assert(host.calls[H_SURFACE_SET] == 1 && host.calls[H_FENCE] == 1 && host.calls[H_WAIT] == 1);
}
static void no_submit(int expected)
{
    assert(draw() == expected);
    assert(!host.npackets && !host.calls[H_SURFACE_SET] && !host.calls[H_FENCE] && !host.calls[H_WAIT]);
}
static void geometry(void)
{
    reset(); assert(!draw()); check_stream(0x00040004, 0x0007000f);
    static const u32 widths[] = {1, 7, 8, 9, 17, 31, 32, 33};
    for (u32 i = 0; i < ARRAY_COUNT(widths); ++i) {
        reset(); rect = (struct gf3_rect){2, 3, widths[i], 5};
        clipping = (struct gf3_rect){0, 0, U32_MAX, U32_MAX};
        assert(!draw()); check_stream(0x00030002, (8U<<16)|(2+widths[i]));
    }
    /* Exactly 128 words, then one byte beyond: repeated 0c00 increment packets. */
    for (u32 extra = 0; extra < 2; ++extra) {
        reset(); surface.width=surface.pitch=5000; surface.bytes=5000*480;
        reservation.value.bytes=surface.bytes;
        rect=(struct gf3_rect){0, 0, 4096+extra*8, 1};
        clipping=(struct gf3_rect){0, 0, 5000, 1}; bits.pitch=513;
        assert(!draw()); check_stream(0, (1U<<16)|rect.w);
        assert(host.npackets == 4+extra);
    }
    reset(); rect=clipping=(struct gf3_rect){0, 0, 640, 480};
    assert(!draw()); check_stream(0, (480U<<16)|640);
    assert(host.npackets == 3+(640U*480/32+127)/128);
    static const enum gf3_fmt formats[] = {GF3_RGB565, GF3_XRGB8888, GF3_ARGB8888, GF3_Y8};
    for (u32 i = 0; i < ARRAY_COUNT(formats); ++i) {
        reset(); surface.fmt=formats[i]; surface.pitch=640*gf3_host_cpp(formats[i]);
        surface.bytes=surface.pitch*480; reservation.value.bytes=surface.bytes;
        assert(!draw()); check_stream(0x00040004, 0x0007000f);
    }
}
static void errors(void)
{
    reset(); rect.w=0; bits.ptr=0; surface.generation=0; no_submit(0);
    reset(); rect.h=0; no_submit(0);
    reset(); clipping.w=0; no_submit(0);
    reset(); clipping.x=U32_MAX; no_submit(0);
    reset(); clipping=(struct gf3_rect){0, 0, 1, 1}; no_submit(0);
    reset(); bits.bytes=4*bits.pitch+2; no_submit(GF3_ERANGE);
    reset(); bits.pitch=2; no_submit(GF3_ERANGE);
    reset(); bits.ptr=0; no_submit(GF3_EINVAL);
    /* Keep geometry valid so this isolates unsupported GDI format. */
    reset(); surface.fmt=GF3_YUY2;
    surface.pitch=surface.width*gf3_host_cpp(surface.fmt);
    surface.bytes=surface.pitch*surface.height;
    reservation.value.bytes=surface.bytes; no_submit(GF3_EFORMAT);
    reset(); surface.generation++; no_submit(GF3_ESTATE);
    reset(); rect.x=640; no_submit(GF3_ERANGE);
    reset(); host.ctx.ready=false; no_submit(GF3_ESTATE);
    reset(); host.ctx.epoch++; no_submit(GF3_ESTATE);
    reset(); host.ch.state=GF2_DRAIN; no_submit(GF3_ESTATE);
    reset(); host.ch.fault.latched=true; no_submit(GF2_EFAULT);
    reset(); surface.width=surface.pitch=65535; surface.bytes=65535*480;
    reservation.value.bytes=surface.bytes; rect=(struct gf3_rect){0, 0, 65529, 1};
    clipping=(struct gf3_rect){0, 0, 65535, 1}; no_submit(GF3_ERANGE);
    reset(); surface.height=65536; surface.bytes=surface.pitch*surface.height;
    reservation.value.bytes=surface.bytes; rect=(struct gf3_rect){0, 65535, 8, 1};
    clipping=rect; no_submit(GF3_ERANGE);
    reset(); surface.width=surface.pitch=65536; surface.bytes=65536*480;
    reservation.value.bytes=surface.bytes; no_submit(GF3_ERANGE);
    reset(); assert(gf3_mono(0, &surface, rect, &bits, fg, bg, clipping, DEADLINE) == GF3_EINVAL);
    assert(gf3_mono(&host.ctx, 0, rect, &bits, fg, bg, clipping, DEADLINE) == GF3_EINVAL);
    assert(gf3_mono(&host.ctx, &surface, rect, 0, fg, bg, clipping, DEADLINE) == GF3_EINVAL);
    host.ctx.ch=0; no_submit(GF3_EINVAL);
    static const enum gf3_host_op ops[] = {H_FAULT, H_SURFACE_CHECK, H_RAM_CHECK, H_SURFACE_SET, H_FENCE, H_WAIT};
    for (u32 i = 0; i < ARRAY_COUNT(ops); ++i) {
        reset(); gf3_host_fail_at(&host, ops[i], 1, GF2_EIO);
        assert(draw() == GF2_EIO && host.calls[ops[i]] == 1);
        if (ops[i] != H_WAIT) assert(!host.calls[H_WAIT]);
    }
    for (u32 i = 1; i <= 5; ++i) {
        reset(); surface.width=surface.pitch=5000; surface.bytes=5000*480;
        reservation.value.bytes=surface.bytes; rect=clipping=(struct gf3_rect){0, 0, 4104, 1};
        bits.pitch=513; gf3_host_fail_at(&host, H_EMIT, i, GF2_EIO);
        assert(draw() == GF2_EIO && host.calls[H_EMIT] == i && host.npackets == i-1);
        assert(!host.calls[H_FENCE] && !host.calls[H_WAIT]);
    }
    reset(); host.complete_after=U32_MAX; host.now=DEADLINE-2;
    assert(draw() == GF2_ETIME && host.now == DEADLINE);
}
/* Guard the byte immediately after a short, odd-sized source. i386 Linux
 * mprotect syscall 125, PROT_NONE=0. No libc or mmap dependency. */
static u8 guarded[8192] __attribute__((aligned(4096)));
static void guard_tail(void)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(125), "b"(guarded+4096),
                     "c"(4096), "d"(0) : "memory", "cc");
    assert(!rc);
    reset(); rect=(struct gf3_rect){2, 3, 9, 3};
    bits=(struct gf3_ram){guarded+4096-8, 8, 3};
    for (u32 i=0; i<bits.bytes; ++i) ((u8 *)bits.ptr)[i]=(u8)(0x81+i);
    assert(!draw()); check_stream(0x00040004, 0x0006000b);
}
void _start(void)
{
    geometry(); errors(); guard_tail(); gf2_host_exit(0);
}
