/* Included by the existing real paging/ledger/HAL fixture. */
#include "geforce_module.h"
#include "gui.h"
#define GF_TEST_DISPLAY 0xd0000000U
#define GF_TEST_BAR0 0xe0000000U
#define GF_TEST_BAR2 0xe1000000U
static int starts, stops, acquires, releases, validates, transfers, cell_checks, route_gf, route_98;
static int decode_safe=1, fail_palette, route;
int host_planar_inits;
static int identify_gf(void) { return 1; }
static int point_gf(void *p) { (void)p; return 0; }
static int console_safe=1, wm_pending;
void pc98_console_set_safe(int safe) { console_safe=safe; }
int pc98_console_safe(void) { return console_safe; }
int pc98_route_safe(void) { return decode_safe && !route; }
int pc98_route_to_gf(void) { route_gf++; route=1; return 0; }
int pc98_route_to_98(void) { route_98++; route=0; console_safe=decode_safe; return decode_safe ? 0 : -1; }
int pc98_route_cell_check(void) { cell_checks++; return pc98_route_safe() ? 0 : -1; }
#include "../../hwdev/gf/internal.h"
/* Run the actual P3 start state machine. These seams replace lower hardware
 * operations, not the acquire-once/validate-on-resume decision. */
static int acquire_gf(struct gf_display_context *c) { (void)c; acquires++; return INIT_FAIL ? -1 : 0; }
static void release_gf(struct gf_display_context *c) { (void)c; releases++; }
static int validate_gf(struct gf_display_context *c) { (void)c; validates++; return 0; }
static const struct gf_display_owner owner_gf={acquire_gf,release_gf,validate_gf};
static struct gf_display_context display_gf={.owner=&owner_gf,.reusable=true,
    .bars={[GF_MODE_VRAM_BAR]={.base=GF_TEST_DISPLAY,.length=GF_MODE_VRAM_BYTES}}};
int gf_init_run(struct gf_init_context *c, struct gf_init_program *p) { (void)c;(void)p;starts++;return 0; }
int gf_display_baseline(struct gf_display_context *c) { c->baseline_valid=true; return 0; }
int gfinit_mode_calc(const struct gfinit_pll_limits *l,const struct gfinit_mode_saved *s,struct gfinit_mode *m)
{ (void)l;(void)s;(void)m;return 0; }
int gf_display_modeset(struct gf_display_context *c) { (void)c;return 0; }
int gf_display_checks(struct gf_display_context *c) { (void)c;return 0; }
int gf_display_palette(struct gf_display_context *c,u32 first,u32 count,const u8 *rgb)
{ (void)c;(void)first;(void)count;(void)rgb;return fail_palette ? -1 : 0; }
int gf_display_blank(struct gf_display_context *c,int v) { c->blank=v;return 0; }
int gf_display_stop(void *v) { struct gf_display_context *c=v;stops++;c->active=false;return 0; }
static struct video_device device_gf={&display_gf,&gf_video_ops};
#include "../../hwdev/gf/production.h"
int gf_production_selected(void) { return 1; }
int gf_production_identify(struct gf_production_resources *r)
{
    *r=(struct gf_production_resources){0};
    r->identity.device=RECOVER_GF_DEVICE;
    r->bars[0]=(struct recover_bar_extent){GF_TEST_BAR0,GF_TEST_BAR0,GF_PRODUCTION_BAR0_BYTES,1};
    r->bars[1]=(struct recover_bar_extent){GF_TEST_DISPLAY|8,GF_TEST_DISPLAY,GF_PRODUCTION_BAR1_BYTES,1};
    r->bars[2]=(struct recover_bar_extent){GF_TEST_BAR2,GF_TEST_BAR2,GF_PRODUCTION_BAR2_BYTES,1};
    return 0;
}
#include "../../drivers/video/geforce/geforce_platform.c"
#include "geforce_reserve.inc"
static int geforce_gui(void) { return host_gui; }
static i32 wm_notice(u32 op,u32 arg,int owner)
{
    CHECK(op==GUI_OP_SESSION_REQUEST && arg==GUI_SESSION_SWITCH_CUI && owner==1);
    wm_pending=1; return 0;
}
static i32 (*g_gui_handler)(u32,u32,int)=wm_notice;
void ring3_wm_enter(void) {}
void ring3_wm_leave(void) {}
#include "geforce_gui_notice.inc"
static void wm_consume(void)
{
    CHECK(wm_pending); wm_pending=0;
    host_user=0; gfx_shutdown();
}
/* Code from P5's actual failure hook is compiled with core, not faked. */
