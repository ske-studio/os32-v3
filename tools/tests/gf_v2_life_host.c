#include "drivers/video/geforce/gf_v2.h"
#include "drivers/video/geforce/geforce_module.h"
#include "hwdev/gf/display/api.h"
static struct gf_v2 g;
static struct gf2_chan ch;
static struct gf_foundation foundation;
static struct gf3_ctx accel;
static struct gf4_ctx display;
static struct gf4_test_ops ops;
static const struct gf4_test_ops *slot = &ops;
static GeforceModuleContext context;
const GeforceModuleContext *geforce_context = &context;
static int seq, ranges, garts, failures, wait_rc, off;
static int clock_now(void *opaque, u32 *out) { (void)opaque; *out = 42; return 0; }
int gf2_fault_check(struct gf2_chan *c) { (void)c; return 0; }
int gf2_fence_emit(struct gf2_chan *c, struct gf2_fence *f, u32 dl) { (void)c; (void)f; (void)dl; return 0; }
int gf2_fence_wait(struct gf2_chan *c, struct gf2_fence f, u32 dl) { (void)c; (void)f; (void)dl; return wait_rc; }
int gf_v2_gart_range(void *a, u32 b, u32 c, u32 d) { (void)a; (void)b; (void)c; (void)d; return 0; }
void gf3_range_query_set(struct gf3_ctx *c, gf3_range_fn fn, void *a) { (void)c; (void)fn; (void)a; ranges = ++seq; }
int gf_v2_gart_start(struct gf_v2 *a, u32 dl) { (void)a; (void)dl; garts = ++seq; return 0; }
static i32 failed(void *owner, i32 rc, u32 dl) { (void)owner; (void)rc; (void)dl; failures++; if (off) ch.state = GF2_OFF; return 0; }
static int test(void)
{
    static const struct gf4_links links = { .failed = failed };
    context.display_out = &slot; ops.ctx = &display; display.ch = &ch; display.accel = &accel;
    display.ready = display.accepting = 1; display.links = &links;
    ch.f = &foundation; ch.state = GF2_RUN; ch.epoch = 2; accel.ready = 1; accel.epoch = 2;
    foundation.io.timing.clock_us = clock_now;
    g.epoch = 1; g.ring.count = 1; g.ring.head = 1; g.ring.e[0].v2 = 7;
    if (gf_v2_start(&g, 100) || g.lost || g.ring.count || g.failed_upto != 7 || ranges != 1 || garts != 2) return 1;
    accel.ready = 0; garts = 0;
    if (gf_v2_start(&g, 100) != GF3_ESTATE || !g.lost || garts) return 2;
    if (gf_v2_stop(&g) || !g.lost || failures) return 3;
    wait_rc = GF2_ETIME;
    if (gf_v2_stop(&g) != OS32_ERR_IO || failures != 1) return 4;
    off = 1;
    if (gf_v2_stop(&g) || failures != 2 || ch.state != GF2_OFF) return 5;
    if (gf_v2_stop(&g) || failures != 2) return 6;
    ch.state = GF2_RUN; ch.epoch++;
    if (gf_v2_stop(&g) || failures != 2) return 7;
    if (gf_v2_rc(GF2_ETIME) != OS32_ERR_TIMEOUT || gf_v2_rc(GF3_EBUSY) != OS32_ERR_BUSY) return 8;
    return 0;
}
void _start(void) { int rc = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
