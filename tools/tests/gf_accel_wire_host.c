/* Ownerless F3 composition -> video start -> F1/F2/F3 and diagnostic bridge.
 * Current F4 module initialization/owner binding is covered by gf4_c3_host.c.
 * Reuse C1's fake PCI/MMIO device and resident OS adapters. Only display
 * preparation/ROM and platform discovery are fixtures; lifecycle, objects,
 * reservations, windows, hooks and dispatch below are product code. */
#define GF3_CINT_C2
#define GF3_CINT_WIRE
#include "gf_accel_c1_host.c"
#include "../../hwdev/gf/command_test.c"
#include "../../hwdev/gf/display/modes.c"

struct gf1_test_ops gf1_module_ops;
GfxCounters gfx_counters;
void palette_shadow_set(int i, u8 r, u8 g, u8 b)
{ (void)i;(void)r;(void)g;(void)b; __builtin_trap(); }
const PaletteEntry *palette_get_all(void) { __builtin_trap(); }
int pc98_route_select_98(void) { return 0; }
int pc98_route_to_98(void) { return 0; }
int pc98_route_to_gf(void) { return 0; }
int pc98_route_cell_check(void) { return 0; }
void pc98_console_set_safe(int value) { (void)value; }
int gf_present_rect(const u8 *src, u32 sp, volatile u8 *dst, u32 dp,
                    volatile u8 *vram, int x, int y, int w, int h,
                    int (*point)(void *), void *owner)
{ (void)src;(void)sp;(void)dst;(void)dp;(void)vram;
  (void)x;(void)y;(void)w;(void)h;(void)point;(void)owner; __builtin_trap(); }
int gf_production_prepare(struct gf_production *p, const struct gf_production_resources *r,
                          int (*route)(void *), void *o)
{ (void)r;(void)route;(void)o;p->prepared=true;return 0; }
int gf_production_identity(struct gf_production *p) { (void)p; __builtin_trap(); }
int gf_production_decode_safe(struct gf_production *p) { (void)p; __builtin_trap(); }
int gf_production_quiesce(struct gf_production *p) { (void)p; __builtin_trap(); }
int gf_production_point(void *p) { (void)p; __builtin_trap(); }
int gf_init_run(struct gf_init_context *c, struct gf_init_program *p)
{ (void)c;(void)p; __builtin_trap(); }
int gf_display_baseline(struct gf_display_context *c) { (void)c; __builtin_trap(); }
int gf_display_modeset(struct gf_display_context *c) { assert(c == &cint_prod.display); return 0; }
int gf_display_checks(struct gf_display_context *c) { assert(c == &cint_prod.display); return 0; }
int gf_display_palette(struct gf_display_context *c, u32 first, u32 n, const u8 *p)
{ (void)c;(void)first;(void)n;(void)p; __builtin_trap(); }
int gf_display_blank(struct gf_display_context *c, int blank)
{ (void)c;(void)blank; __builtin_trap(); }
int gfinit_mode_calc(const struct gfinit_pll_limits *l, const struct gfinit_mode_saved *b,
                     struct gfinit_mode *out)
{ (void)l;(void)b; *out = (struct gfinit_mode){0}; return 0; }
static int wire_validate(struct gf_display_context *c)
{ assert(c == &cint_prod.display); return 0; }
static int wire_foundation_start(struct gf_display_context *c)
{ assert(c == &cint_prod.display); return gf_foundation_start(&cint_prod.foundation); }
static const struct gf_display_owner wire_owner = {
    .validate = wire_validate, .foundation_start = wire_foundation_start,
    .foundation_stop = cint_foundation_stop, .command_get = cint_command_get
};
static int wire_call(enum gf2_test_op op, u32 deadline)
{
    struct gf2_test_request q = {sizeof(q), op, deadline};
    struct gf2_test_reply reply;
    int rc = gf2_module_ops.call(&cint_prod.foundation, &q, &reply);
    assert(rc == reply.rc);
    return rc;
}
static void wire_video_start(void)
{
    u8 palette[VIDEO_PALETTE_BYTES] = {0};
    cint_prod.display.owner = &wire_owner;
    cint_prod.display.initialized = true;
    cint_prod.display.bars[GF_MODE_VRAM_BAR] = cint_prod.chan.bar1;
    assert(!gf_video_ops.start(&cint_prod.display, 1, palette));
}
void _start(void)
{
    /* No service registration: command bridge refuses all lifecycle ops
     * before touching F2, and video start cannot publish layer 3. */
    cint_reset();
    gf2_test_bind_production(&cint_prod.foundation, &cint_prod.chan);
    for (unsigned op = GF2T_START; op <= GF2T_RECOVER; ++op)
        if (op == GF2T_START || op == GF2T_STOP || op == GF2T_RECOVER)
            assert(wire_call(op, cint_deadline()) == GF3_ESTATE);
    assert(cint_prod.chan.state == GF2_RUN);
    assert(!gf2_stop(&cint_prod.chan));
    wire_video_start();
    assert(!gf3_service_get(&cint_prod.chan) && !cint_prod.accel.ready);
    cint_closed();
    assert(!gf2_stop(&cint_prod.chan));

    GfxBackend backend;
    const struct gf1_test_ops *foundation_slot = 0;
    struct gf_production_resources boot_resources={0};
    struct geforce_platform platform={.resources=&boot_resources};
    GeforceModuleContext context = {.platform=&platform, .backend = &backend, .accel_out = &cint_slot,
        .foundation_out = &foundation_slot};
    assert(!gf_ownerless_module_init(&backend, &context));
    cint_service = gf3_service_get(&cint_prod.chan);
    assert(cint_service && cint_service->slot == &cint_slot);
    assert(foundation_slot == &gf1_module_ops);
    assert(!cint_service->owner && !cint_prod.display.display4);
    wire_video_start();
    assert(cint_slot && hook_active && cint_prod.accel.ready);
    cint_owned_reservations();
    cint_open_windows();
    u32 id = cint_service->scan.reservation_id;
    cint_trace_clear(); cint_expect_closed = true;
    assert(!wire_call(GF2T_STOP, cint_deadline()));
    cint_closed(); cint_owned_reservations();
    assert(cint_prod.chan.state == GF2_OFF && !cint_prod.accel.ready && !views);
    assert(cint_prod.display.engaged && !cint_restore_calls && !cint_owner_stops);
    assert(cint_close_calls[0] == 1 && cint_close_calls[1] == 1);
    cint_expect_closed = false;
    assert(!gf_foundation_start(&cint_prod.foundation));
    assert(!wire_call(GF2T_START, cint_deadline()));
    assert(cint_slot && hook_active && cint_prod.accel.ready);
    assert(cint_service->scan.reservation_id == id);

    struct gf3_surface old = cint_service->test;
    u32 epoch = cint_prod.chan.epoch, generation = cint_service->scan.generation;
    cint_open_windows();
    ++cint_prod.foundation.stats.graph;
    assert(gf2_fault_check(&cint_prod.chan) == GF2_EFAULT);
    /* The bridge snapshots hardware after recover has republished. Window
     * callbacks independently assert closure while cleanup is in progress. */
    cint_expect_closed = false;
    assert(!wire_call(GF2T_RECOVER, cint_deadline()));
    cint_expect_closed = false;
    assert(cint_slot && hook_active && cint_prod.accel.ready && !views);
    assert(cint_prod.chan.epoch == epoch + 1 && cint_prod.accel.epoch == epoch + 1);
    assert(cint_service->scan.generation == generation + 1);
    assert(cint_service->scan.reservation_id != id && cint_service->test.reservation_id != old.reservation_id);
    assert(gf3_fill(&cint_prod.accel, &old, (struct gf3_rect){0,0,1,1}, 1, 3, 0xff, cint_deadline()) == GF3_ESTATE);
    cint_stop_display();
    gf2_host_exit(0);
}
