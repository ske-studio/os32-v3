#define GF3_HOST_UNIT 10
#include "gf_accel_host.h"

static struct gf3_host host;
static struct gf3_host_event events[4];
static u8 vram[64], pixels[12];

void _start(void)
{
    struct gf3_surface scan = { .off = 8, .bytes = 32, .pitch = 8,
        .width = 4, .height = 4, .fmt = GF3_INDEX8 };
    struct gf3_rect r = { 1, 1, 2, 2 };
    struct gf3_ram ram = { pixels, sizeof(pixels), 4 };
    const u32 deadline = 12345;
    gf3_host_init(&host);
    host.events = events; host.cevents = 4;
    host.vram = vram; host.vram_bytes = sizeof(vram);
    pixels[0] = 11; pixels[1] = 12; pixels[4] = 21; pixels[5] = 22;
    assert(gf3_present_m2mf(&host.ctx, &scan, r, &ram, deadline) == 0);
    assert(host.nevents == 1 && events[0].op == H_UPLOAD);
    assert(events[0].p[0] == &host.ctx && events[0].p[1] == &scan && events[0].p[2] == &ram);
    assert(events[0].a[0] == r.x && events[0].a[1] == r.y &&
           events[0].a[2] == r.w && events[0].a[3] == r.h &&
           events[0].a[4] == true && events[0].a[5] == deadline);
    for (u32 i = 0; i < sizeof(vram); ++i) {
        u8 expected = i == 17 ? 11 : i == 18 ? 12 : i == 25 ? 21 : i == 26 ? 22 : 0;
        assert(vram[i] == expected);
    }
    gf3_host_trace_clear(&host);
    gf3_host_fail_at(&host, H_UPLOAD, 1, GF2_ETIME);
    assert(gf3_present_m2mf(&host.ctx, &scan, r, &ram, deadline) == GF2_ETIME);
    assert(host.nevents == 1 && events[0].op == H_UPLOAD && events[0].rc == GF2_ETIME);
    gf2_host_exit(0);
}
