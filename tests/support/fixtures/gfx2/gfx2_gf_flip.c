#include "sys/dev/video/geforce/gf_v2.h"
#include "sys/dev/video/geforce/geforce_module.h"
#include "sys/dev/video/geforce/display/api.h"

static struct gf4_state state;
static struct gf_foundation foundation;
static struct gf2_chan channel = {.f = &foundation, .heads = 1};
static struct gf_v2 backend;
static struct gfx2_surf target = {.sid = 1025, .fmt = GFX2_FMT_INDEX8,
                                 .gpu_off = 4096, .priv = {2, 1}};
static u32 base, writes, vblank;
static int fence_rc, read_rc;

static i32 changed(void *ctx, u8 head, const struct gf3_surface *front) { return 0; }
static const struct gf4_links links = {.scan_changed = changed};
static struct gf4_ctx ctx = {.ch = &channel, .state = &state, .links = &links,
                             .ready = true, .accepting = true};
static const struct gf4_test_ops ops = {.ctx = &ctx, .flip = gf4_flip};
static const struct gf4_test_ops *output = &ops;
static const GeforceModuleContext module = {.display_out = &output};
const GeforceModuleContext *geforce_context = &module;
int serial_gate_active(void) { return 1; }
void serial_puts_polled(const char *s) {}
int sys_time_now(u32 *lo, u32 *hi) { *lo = *hi = 0; return 0; }
int gf2_fault_check(struct gf2_chan *ch) { return 0; }
i32 gf4_surface_check(struct gf4_ctx *c, const struct gf3_surface *s,
                      const struct gf4_mode *m) { return 0; }
int gf2_fence_emit(struct gf2_chan *ch, struct gf2_fence *f, u32 dl) { return fence_rc; }
int gf2_fence_wait(struct gf2_chan *ch, struct gf2_fence f, u32 dl) { return 0; }
void gf_irq_snapshot(struct gf_foundation *f, struct gf_irq_stats *stats)
{
    stats->vblank[0] = vblank;
}
static int clock_us(void *p, u32 *now) { *now = 0; return 0; }
static int wait_us(void *p, u32 us) { ++vblank; return 0; }
static int read_base(void *p, u32 bar, u32 off, u32 width, u32 *v)
{
    *v = base;
    return read_rc;
}
static int write_base(void *p, u32 bar, u32 off, u32 width, u32 v)
{
    ++writes;
    base = v;
    return 0;
}
static int run(void)
{
    u32 retired = 0;
    foundation.running = true;
    foundation.wanted_heads = 1;
    foundation.io.read = read_base;
    foundation.io.write = write_base;
    foundation.io.timing.clock_us = clock_us;
    foundation.io.timing.wait_us = wait_us;
    state.head[0].enabled = true;
    if (gf_v2_flip(&backend, 0, &target, 0, 100) != OS32_ERR_INVAL ||
        writes || base || backend.head[0].scan_sid || backend.head[0].pend_sid ||
        backend.head[0].retired_sid || ctx.hold[0].pending) return 6;
    backend.head[0].scan_sid = 1024;
    backend.node[0].value = (struct gf_vram_reservation){
        .owner = &target, .reservation_id = 2, .generation = 1
    };
    fence_rc = GF2_ETIME;
    if (gf_v2_flip(&backend, 0, &target, 0, 100) != OS32_ERR_TIMEOUT ||
        writes || backend.head[0].pend_sid || ctx.hold[0].pending) return 1;
    fence_rc = 0;
    read_rc = GF2_EIO;
    if (gf_v2_flip(&backend, 0, &target, 0, 100) != OS32_ERR_AGAIN ||
        writes != 1 || backend.head[0].pend_sid != target.sid ||
        !ctx.hold[0].pending) return 2;
    if (!gf_v2_flip_done(&backend, 0, &retired) || retired ||
        backend.head[0].scan_sid != 1024 || backend.head[0].pend_sid != target.sid) return 3;
    read_rc = 0;
    if (gf_v2_flip_done(&backend, 0, &retired) || retired != 1024 ||
        !vblank || backend.head[0].pend_sid || state.head[0].front.off != base) return 4;
    if (gf_v2_flip_done(&backend, 0, &retired) || retired) return 4;
    if (gf_v2_flip(&backend, 0, &target, 0, 100) ||
        gf_v2_flip_done(&backend, 0, &retired) != OS32_ERR_BUSY) return 5;
    ++vblank;
    if (gf_v2_flip_done(&backend, 0, &retired) || retired != target.sid) return 5;
    return 0;
}
void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(run()));
    __builtin_unreachable();
}
