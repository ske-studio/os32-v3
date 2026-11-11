#include "gfx2_bridge_fake.h"
/* G3: real scan publication, eligibility, policy, query and present; fake GPU. */
#define OS32_GEFORCE_MODULE
#include "../../drivers/video/geforce/backend_geforce.c"
#undef gfx_backend_geforce
#undef gfx_current_height
#undef gfx_flip_enabled
#undef gfx_display_page
#include "../../gfx/gfx_policy.c"
#include "../../drivers/video/geforce/gf_direct.c"
#include "../../drivers/video/geforce/gf_hook.c"
#undef gfx_counters

static int started = 1, gui = 1, owner = GFX_OWNER_WM, revokes;
static int failures, point_rc, draw_rc;
static struct gf_foundation foundation;
static struct gf2_chan channel = {.f=&foundation, .state=GF2_RUN};
static struct gf3_ctx accel = {.ch=&channel};
static struct gf4_ctx display = {.ch=&channel, .accel=&accel};
static const struct gf4_test_ops display_table = {.ctx=&display};
static const struct gf4_test_ops *dispatch = &display_table;
static u8 ram[GF_MODE_VRAM_BYTES];
static struct geforce_platform platform = {.display_phys=0x10000000};
GfxCounters gfx_counters;
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
static GfxBackend backend = {.direct_ops=&gf_direct_ops, .direct_ok=geforce_direct_ok,
    .bb_base=ram, .bb_pitch=GF_WIDTH, .bb_size=sizeof(ram)};
GfxBackend *g_backend = &backend;
static GeforceModuleContext context = {.backend=&backend, .platform=&platform,
    .height=&gfx_current_height, .counters=&gfx_counters, .display_out=&dispatch};
const GeforceModuleContext *geforce_context = &context;
int gfx_is_started(void) { return started; }
int con_sink_is_enabled(void) { return gui; }
int appslot_gfx_owner(void) { return owner; }
void gfx_reinit_surface_roles(int finish, int display_only)
{ (void)display_only; revokes += !finish; }
static int point(void *opaque) { (void)opaque; return point_rc; }
static int clock_us(void *opaque, u32 *now) { (void)opaque; *now=1; return 0; }
void geforce_display_failed(void) { ++failures; gui=0; }
/* Enter while already active must only reevaluate, never initialize hardware. */
int video_mode_info(struct video_device *d,u32 i,struct video_mode *m)
{ (void)d;(void)i;(void)m; __builtin_trap(); }
int video_framebuffer(struct video_device *d,struct video_fb *f)
{ (void)d;(void)f; __builtin_trap(); }
int video_start(struct video_device *d,u32 id,const u8 *p)
{ (void)d;(void)id;(void)p; __builtin_trap(); }
const PaletteEntry *palette_get_all(void) { __builtin_trap(); }
void pc98_console_set_safe(int safe) { (void)safe; __builtin_trap(); }
int pc98_route_cell_check(void) { __builtin_trap(); }
int pc98_route_to_gf(void) { __builtin_trap(); }
int pc98_route_to_98(void) { return backend.policy == GFX_POLICY_SHADOW ? 0 : -1; }
int gf_foundation_stop(struct gf_foundation *f) { (void)f; __builtin_trap(); }
int video_stop(struct video_device *d) { (void)d; return 0; }
int gf_present_rect(const u8 *a,u32 b,volatile u8 *c,u32 d,volatile u8 *e,
    int x,int y,int w,int h,int (*p)(void *),void *o)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)x;(void)y;(void)w;(void)h;(void)p;(void)o; return -1; }
/* Fake GPU: channel stop and fence timeout reach the real HAL hook. */
int gf3_fill(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,
             u32 color,u8 alu,u32 mask,u32 dl)
{
    (void)s;(void)r;(void)color;(void)alu;(void)mask;(void)dl;
    return c->ch->state == GF2_RUN ? draw_rc : GF3_ESTATE;
}
static void finish(int rc)
{ __asm__ volatile("int $0x80" : : "a"(1),"b"(rc)); __builtin_unreachable(); }
#define CHECK(x) do { if (!(x)) finish(1); } while (0)
static void check_policy(int direct)
{
    GFX_ScreenInfo info;
    CHECK(!geforce_query(&info));
    CHECK(!!(info.flags & GFX_CAP_DIRECT) == direct);
    CHECK(gfx_policy_blocks_map(LEDGER_ROLE_CLIENT) == direct);
    CHECK(gfx_policy_blocks_map(LEDGER_ROLE_DISPLAY) == direct);
    CHECK(info.reserved[0] == (direct ? gfx_policy_gen_get() : 0));
    CHECK(backend.fill_rect == (direct ? gf_direct_ops.fill_rect : 0));
    CHECK(backend.blit == (direct ? gf_direct_ops.blit : 0));
}
void _start(void)
{
    struct gf3_surface front = {.off=0,.bytes=GF_MODE_VRAM_BYTES,.pitch=GF_WIDTH,
        .width=GF_WIDTH,.height=GF_HEIGHT,.fmt=GF3_INDEX8};
    foundation.io.timing.clock_us = clock_us;
    interrupt_point = point;
    platform.point = point;
    gf_hook_ctx_bind(&accel);
    active = routed = 1;
    gf_hook_scan_changed(0, &front, 0);
    check_policy(1); CHECK(revokes == 1);
    u16 gen = gfx_policy_gen_get();
    gf_hook_scan_changed(0, &front, 0);
    CHECK(gfx_policy_gen_get() != gen); check_policy(1);
    geforce_present(0,0,1,1);
    CHECK(gfx_counters.commits == 1 && gfx_counters.present_bytes == 0);
    owner = 7; gfx_policy_reeval(); check_policy(0);
    int held_revokes = revokes;
    gf_hook_scan_changed(0, &front, 0); check_policy(0);
    CHECK(revokes == held_revokes);
    owner = GFX_OWNER_WM; gfx_policy_reeval(); check_policy(1);
    front.off = GF_MODE_VRAM_BYTES; gf_hook_scan_changed(0,&front,0); check_policy(0);
    front.off = 0; gf_hook_scan_changed(0,&front,0); check_policy(1);
    started = 0; gfx_policy_reeval(); check_policy(0);
    started = 1; gfx_policy_reeval(); check_policy(1);
    gui = 0; gfx_policy_reeval(); check_policy(0);
    gui = 1; gen = gfx_policy_gen_get();
    geforce_enter(); check_policy(1); CHECK(gfx_policy_gen_get() != gen);
    CHECK(!geforce_stop()); check_policy(0);
    /* Each case starts with a fresh fake device, as a new driver lifetime. */
    for (int fault = 0; fault < 4; ++fault) {
        gui = 1; failed = 0; active = routed = 1;
        channel.state = GF2_RUN; accel.last_rc = draw_rc = point_rc = 0;
        gf_hook_scan_changed(0, &front, 0); check_policy(1);
        gen = gfx_policy_gen_get();
        if (fault == 0) channel.state = GF2_FAULT;
        if (fault == 1) draw_rc = GF2_ETIME;
        if (fault < 2) {
            CHECK(backend.fill_rop(0,0,1,1,3,GFX_ROP_COPY) == OS32_ERR_IO);
            CHECK(accel.last_rc == (fault ? GF2_ETIME : GF3_ESTATE));
        } else {
            u8 pixel = 0;
            point_rc = OS32_ERR_IO;
            if (fault == 2) CHECK(backend.put(0,0,1,1,&pixel,1,-1) == OS32_ERR_IO);
            else CHECK(backend.get(0,0,1,1,&pixel,1) == OS32_ERR_IO);
        }
        u32 commits = gfx_counters.commits;
        geforce_present(0,0,1,1);
        check_policy(0);
        CHECK(gfx_policy_gen_get() != gen && failed && !active && !routed);
        CHECK(failures == fault + 1 && !gui);
        CHECK(gfx_counters.commits == commits);
    }
    finish(0);
}
