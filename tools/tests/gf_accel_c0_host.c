/* C0: link gf_hook.c as the sole product TU, GF3_HOST_UNIT=11 dependencies.
 * G5 clipping expectations: backend_cirrus.c:403-406,492-505. The fixture
 * uses the fixed 640x480 INDEX8 mode with padding and surrounding sentinels.
 * No libc/CRT; A2 registers execution and mutations after integration. */
#define GF3_HOST_UNIT 11
#include "gf_accel_host.h"
#include "gfx_hal.h"

enum { WIDTH = 640, HEIGHT = 480, PITCH = WIDTH + 8,
       PREFIX = 16, SUFFIX = 16, SCAN_BYTES = PITCH * HEIGHT,
       VRAM_BYTES = PREFIX + SCAN_BYTES + SUFFIX, EVENT_CAPACITY = 8 };
GfxCounters gfx_counters;
int geforce_fill_rop(int, int, int, int, u8, u8);
static struct gf3_host host;
static struct gf3_host_event events[EVENT_CAPACITY];
static u8 vram[VRAM_BYTES], before[VRAM_BYTES], expected[VRAM_BYTES];
static struct gf3_surface scan = {
    .off = PREFIX, .bytes = SCAN_BYTES, .pitch = PITCH,
    .width = WIDTH, .height = HEIGHT, .fmt = GF3_INDEX8,
    .reservation_id = 7, .generation = 2
};
static struct gf3_rect wanted;
static u32 wanted_dx, wanted_dy, wanted_color, wanted_alu = 3;

static void observe(struct gf3_host *h, const struct gf3_host_event *e)
{
    if (e->op != H_FILL && e->op != H_COPY) return;
    const struct gf3_surface *s = e->p[1];
    assert(e->p[0] == &h->ctx && s != &scan);
    assert(s->off == PREFIX && s->bytes == SCAN_BYTES && s->pitch == PITCH);
    assert(s->width == WIDTH && s->height == HEIGHT && s->fmt == GF3_INDEX8);
    assert(s->reservation_id == 7 && s->generation == 2);
    assert(e->a[0] == wanted.x && e->a[1] == wanted.y);
    assert(e->a[2] == wanted.w && e->a[3] == wanted.h);
    if (e->op == H_FILL) {
        assert(e->a[4] == wanted_color && e->a[5] == wanted_alu && e->a[6] == U8_MAX);
        assert(e->a[7] == (u32)(h->now + GF3_TIMEOUT_US));
    } else {
        assert(e->p[2] == s && e->a[4] == wanted_dx && e->a[5] == wanted_dy);
        assert(e->a[6] == 3 && e->a[7] == U8_MAX);
        assert(e->a[8] == (u32)(h->now + GF3_TIMEOUT_US));
    }
}

static void reset_image(void)
{
    gf3_host_trace_clear(&host);
    gfx_counters.hw_ops = 0;
    for (u32 i = 0; i < VRAM_BYTES; ++i) vram[i] = (u8)(i ^ (i >> 8));
    gf2_host_memcpy(before, vram, sizeof(vram));
    gf2_host_memcpy(expected, vram, sizeof(vram));
}

static void check_fill(int x, int y, int w, int h, struct gf3_rect r)
{
    reset_image(); wanted = r; wanted_color = 0xa7;
    assert(!geforce_fill_rect(x, y, w, h, wanted_color));
    assert(host.calls[H_FILL] == 1 && host.calls[H_CLOCK] == 1);
    assert(host.ctx.last_rc == GF3_OK && gfx_counters.hw_ops == 1);
    for (u32 yy = r.y; yy < r.y+r.h; ++yy)
        for (u32 xx = r.x; xx < r.x+r.w; ++xx)
            expected[PREFIX+yy*PITCH+xx] = wanted_color;
    assert(!gf2_host_memcmp(vram, expected, sizeof(vram)));
}

static void check_copy(int dx, int dy, int sx, int sy, int w, int h,
                       struct gf3_rect r)
{
    reset_image(); wanted = r; wanted_dx = dx; wanted_dy = dy;
    assert(!geforce_blit(dx, dy, sx, sy, w, h));
    assert(host.calls[H_COPY] == 1 && host.calls[H_CLOCK] == 1);
    assert(host.ctx.last_rc == GF3_OK && gfx_counters.hw_ops == 1);
    for (u32 y = 0; y < r.h; ++y)
        for (u32 x = 0; x < r.w; ++x)
            expected[PREFIX+((u32)dy+y)*PITCH+(u32)dx+x] =
                before[PREFIX+(r.y+y)*PITCH+r.x+x];
    assert(!gf2_host_memcmp(vram, expected, sizeof(vram)));
}

void _start(void)
{
    gf3_host_init(&host);
    host.events = events; host.cevents = EVENT_CAPACITY;
    host.vram = vram; host.vram_bytes = sizeof(vram); host.observe = observe;
    /* Wrap the absolute deadline to verify modulo-u32 clock arithmetic. */
    host.now = U32_MAX - GF3_TIMEOUT_US/2;
    gf_hook_ctx_bind(&host.ctx);
    assert(geforce_fill_rect(0, 0, 1, 1, 0) == OS32_ERR_NOSYS);
    assert(geforce_blit(-1, 0, 0, 0, 1, 1) == OS32_ERR_NOSYS);
    struct gf3_surface borrowed = scan;
    gf_hook_scan_bind(true, &borrowed);
    borrowed = (struct gf3_surface){0}; /* adapter must retain its own copy */
    check_fill(639, 479, S32_MAX, S32_MAX, (struct gf3_rect){639, 479, 1, 1});
    check_fill(-3, -2, 5, 4, (struct gf3_rect){0, 0, 2, 2});
    check_fill(2, 3, 4, 5, (struct gf3_rect){2, 3, 4, 5});
    reset_image(); wanted = (struct gf3_rect){1, 2, 3, 4};
    wanted_color = 0xa7; wanted_alu = 6;
    assert(!geforce_fill_rop(1, 2, 3, 4, wanted_color, GFX_ROP_XOR));
    for (u32 y = 2; y < 6; ++y)
        for (u32 x = 1; x < 4; ++x) expected[PREFIX+y*PITCH+x] ^= wanted_color;
    assert(gfx_counters.hw_ops == 1 && !gf2_host_memcmp(vram, expected, sizeof(vram)));
    reset_image();
    assert(geforce_fill_rop(1, 2, 3, 4, 0, 2) == OS32_ERR_INVAL);
    assert(!gfx_counters.hw_ops && !host.nevents);
    wanted_alu = 3;
    check_copy(639, 479, 2, 3, S32_MAX, S32_MAX, (struct gf3_rect){2, 3, 1, 1});
    check_copy(2, 3, 639, 479, S32_MAX, S32_MAX, (struct gf3_rect){639, 479, 1, 1});
    /* All overlap directions, including same origin, use the same surface. */
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            check_copy(10+x, 10+y, 10, 10, 8, 8, (struct gf3_rect){10, 10, 8, 8});
    reset_image();
    assert(!geforce_fill_rect(0, 0, 0, 1, 1));
    assert(!geforce_fill_rect(0, 0, 1, -1, 1));
    assert(!geforce_fill_rect(WIDTH, 0, 1, 1, 1));
    assert(!geforce_fill_rect(0, HEIGHT, 1, 1, 1));
    assert(!geforce_fill_rect(-S32_MAX-1, 0, S32_MAX, 1, 1));
    assert(!geforce_fill_rect(0, -S32_MAX-1, 1, S32_MAX, 1));
    assert(!geforce_blit(0, 0, 0, 0, -1, 1));
    assert(!geforce_blit(0, 0, 0, 0, 1, 0));
    assert(!geforce_blit(WIDTH, 0, 0, 0, 1, 1));
    assert(!geforce_blit(0, HEIGHT, 0, 0, 1, 1));
    assert(!geforce_blit(0, 0, WIDTH, 0, 1, 1));
    assert(!geforce_blit(0, 0, 0, HEIGHT, 1, 1));
    assert(geforce_blit(-1, 0, 0, 0, 1, 1) == OS32_ERR_INVAL);
    assert(geforce_blit(0, -1, 0, 0, 1, 1) == OS32_ERR_INVAL);
    assert(geforce_blit(0, 0, -1, 0, 1, 1) == OS32_ERR_INVAL);
    assert(geforce_blit(0, 0, 0, -1, 1, 1) == OS32_ERR_INVAL);
    assert(host.nevents == 0 && !gfx_counters.hw_ops && !gf2_host_memcmp(vram, before, sizeof(vram)));
    wanted = (struct gf3_rect){0, 0, 1, 1}; wanted_color = 9;
    gf3_host_fail_at(&host, H_FILL, 1, GF2_ETIME);
    assert(geforce_fill_rect(0, 0, 1, 1, 9) == OS32_ERR_IO);
    assert(host.ctx.last_rc == GF2_ETIME);
    assert(!gf2_host_memcmp(vram, before, sizeof(vram)));
    gf3_host_fail_at(&host, H_FILL, 0, 0);
    reset_image(); wanted_dx = wanted_dy = 0;
    gf3_host_fail_at(&host, H_COPY, 1, GF2_EFAULT);
    assert(geforce_blit(0, 0, 0, 0, 1, 1) == OS32_ERR_IO);
    assert(host.ctx.last_rc == GF2_EFAULT);
    assert(!gf2_host_memcmp(vram, before, sizeof(vram)));
    gf3_host_fail_at(&host, H_COPY, 0, 0);
    reset_image(); gf3_host_fail_at(&host, H_CLOCK, 0, GF1_EIO);
    assert(geforce_fill_rect(0, 0, 1, 1, 9) == OS32_ERR_IO);
    assert(geforce_blit(0, 0, 0, 0, 1, 1) == OS32_ERR_IO);
    assert(host.ctx.last_rc == GF2_EIO && !host.calls[H_FILL] && !host.calls[H_COPY]);
    assert(!gf2_host_memcmp(vram, before, sizeof(vram)));
    gf3_host_fail_at(&host, H_CLOCK, 0, 0);
    reset_image(); gf_hook_scan_bind(false, &scan);
    assert(geforce_fill_rect(0, 0, 1, 1, 9) == OS32_ERR_NOSYS);
    assert(geforce_blit(0, 0, 0, 0, 1, 1) == OS32_ERR_NOSYS);
    gf_hook_scan_bind(false, NULL);
    assert(geforce_fill_rect(0, 0, 1, 1, 9) == OS32_ERR_NOSYS);
    assert(geforce_blit(0, 0, 0, 0, 1, 1) == OS32_ERR_NOSYS);
    gf_hook_scan_bind(true, &scan); gf_hook_ctx_bind(NULL);
    assert(geforce_fill_rect(0, 0, 1, 1, 9) == OS32_ERR_NOSYS);
    assert(geforce_blit(0, 0, 0, 0, 1, 1) == OS32_ERR_NOSYS);
    assert(host.nevents == 0);
    gf2_host_exit(0);
}
