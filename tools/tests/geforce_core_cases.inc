/* Included by the existing real paging/ledger/HAL fixture. */
#include "geforce_module.h"
#define GF_TEST_DISPLAY 0xe4000000U
static int starts, stops, acquires, releases, validates, transfers, cell_checks, route_gf, route_98;
static int decode_safe=1, fail_palette;
int host_planar_inits;
static int identify_gf(void) { return 1; }
static int point_gf(void *p) { (void)p; return 0; }
int pc98_route_safe(void) { return decode_safe; }
int pc98_route_to_gf(void) { route_gf++; return 0; }
int pc98_route_to_98(void) { route_98++; return decode_safe ? 0 : -1; }
int pc98_route_cell_check(void) { cell_checks++; return decode_safe ? 0 : -1; }
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
static int reserve_gf(void)
{
    struct ledger_resource r={.decode_first=GF_TEST_DISPLAY/PAGE_SIZE,
        .decode_end=(GF_TEST_DISPLAY+MEM_GFX_BB8_SIZE)/PAGE_SIZE,
        .map_first=GF_TEST_DISPLAY/PAGE_SIZE,.map_end=(GF_TEST_DISPLAY+MEM_GFX_BB8_SIZE)/PAGE_SIZE,
        .bus=LEDGER_BUS_FIXED,.width_basis=LEDGER_WB_DATASHEET};
    u32 rid;
    CHECK(ledger_resource_add(&r,&rid));
    struct ledger_span sp={r.decode_first,r.decode_end,LEDGER_SPAN_MMIO,rid};
    CHECK(ledger_reserve_set(LEDGER_OWNER_GFX,&sp,1));
    CHECK(!paging_map_phys(GF_TEST_DISPLAY,GF_TEST_DISPLAY,MEM_GFX_BB8_SIZE/PAGE_SIZE,PAGE_RW|PTE_PCD));
    CHECK(ledger_resource_set_map(rid,r.map_first,r.map_end));
    return 1;
}
static const struct geforce_platform platform_gf={.device=&device_gf,.reserve=reserve_gf,
    .identify=identify_gf,.pc98_safe=pc98_route_safe,.point=point_gf,.display_phys=GF_TEST_DISPLAY};
const struct geforce_platform *geforce_platform_get(void) { return &platform_gf; }
#include "geforce_reserve.inc"
static int geforce_gui(void) { return host_gui; }
/* Code from P5's actual failure hook is compiled with core, not faked. */
