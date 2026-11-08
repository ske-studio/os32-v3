/* G3: real scan publication, eligibility, policy, query and present; fake GPU. */
#define OS32_GEFORCE_MODULE
#include "../../drivers/video/geforce/backend_geforce.c"
#undef gfx_backend_geforce
#undef gfx_current_height
#undef gfx_flip_enabled
#undef gfx_display_page
#include "../../gfx/gfx_policy.c"
#include "../../drivers/video/geforce/gf_direct.c"

static int started = 1, gui = 1, owner = GFX_OWNER_WM, revokes;
static u8 ram[GF_MODE_VRAM_BYTES];
static struct geforce_platform platform = {.display_phys=0x10000000};
GfxCounters gfx_counters;
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
static GfxBackend backend = {.direct_ops=&gf_direct_ops, .direct_ok=geforce_direct_ok,
    .bb_base=ram, .bb_pitch=GF_WIDTH, .bb_size=sizeof(ram)};
GfxBackend *g_backend = &backend;
static GeforceModuleContext context = {.backend=&backend, .platform=&platform,
    .height=&gfx_current_height, .counters=&gfx_counters};
const GeforceModuleContext *geforce_context = &context;
int gfx_is_started(void) { return started; }
int con_sink_is_enabled(void) { return gui; }
int appslot_gfx_owner(void) { return owner; }
void gfx_reinit_surface_roles(int finish, int display_only)
{ (void)display_only; revokes += !finish; }
void gf_hook_scan_bind(bool on, const struct gf3_surface *s) { (void)on; (void)s; }
int pc98_route_to_98(void) { return backend.policy == GFX_POLICY_SHADOW ? 0 : -1; }
int gf_foundation_stop(struct gf_foundation *f) { (void)f; __builtin_trap(); }
int video_stop(struct video_device *d) { (void)d; return 0; }
int gf_present_rect(const u8 *a,u32 b,volatile u8 *c,u32 d,volatile u8 *e,
    int x,int y,int w,int h,int (*p)(void *),void *o)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)x;(void)y;(void)w;(void)h;(void)p;(void)o; return -1; }
int geforce_fill_rop(int x,int y,int w,int h,u8 c,u8 r)
{ (void)x;(void)y;(void)w;(void)h;(void)c;(void)r; return 0; }
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
    active = routed = 1;
    gf_hook_scan_changed(0, &front, 0);
    check_policy(1); CHECK(revokes == 1);
    u16 gen = gfx_policy_gen_get();
    gf_hook_scan_changed(0, &front, 0);
    CHECK(gfx_policy_gen_get() != gen); check_policy(1);
    geforce_present(0,0,1,1);
    CHECK(gfx_counters.commits == 1 && gfx_counters.present_bytes == 0);
    owner = 7; gfx_policy_reeval(); check_policy(0);
    gf_hook_scan_changed(0, &front, 0); check_policy(0);
    owner = GFX_OWNER_WM; gfx_policy_reeval(); check_policy(1);
    front.off = GF_MODE_VRAM_BYTES; gf_hook_scan_changed(0,&front,0); check_policy(0);
    front.off = 0; gf_hook_scan_changed(0,&front,0); check_policy(1);
    started = 0; gfx_policy_reeval(); check_policy(0);
    started = 1; gfx_policy_reeval(); check_policy(1);
    CHECK(!geforce_stop()); check_policy(0);
    finish(0);
}
