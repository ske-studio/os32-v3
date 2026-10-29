/* P5 module adapter + real P4 owner/transport/runner, with fake ports/copy. */
#include "kernel/geforce_module.h"
#include "gfx/palette.h"
#define production prod
#include "kernel/geforce_body.c"
#undef production
static GfxBackend hal_backend;
static GfxCounters hal_counters;
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
static int hal_route,hal_safe=1,hal_copies,hal_cells;
static PaletteEntry hal_colors[VIDEO_PALETTE_ENTRIES];
const PaletteEntry *palette_get_all(void) { return hal_colors; }
void palette_shadow_set(int i,u8 r,u8 g,u8 b) { (void)i;(void)r;(void)g;(void)b; }
void pc98_console_set_safe(int safe) { hal_safe=safe; }
int pc98_route_select_98(void) { hal_route=0;return prod_route(&prod); }
int pc98_route_cell_check(void)
{
    CHECK(!hal_route && !gf_production_decode_safe(&prod));
    hal_cells++;prod_event(0xce11);return 0;
}
int pc98_route_to_gf(void)
{ CHECK(hal_copies && hal_cells && !hal_route);hal_route=1;prod_event(0xfac01);return 0; }
int pc98_route_to_98(void)
{
    int rc=pc98_route_select_98();
    hal_safe=!rc && !gf_production_decode_safe(&prod);
    if (!hal_safe) return -1;
    prod_redraws++;prod_event(PROD_REDRAW);return 0;
}
int gf_present_rect(const u8 *c,u32 cp,volatile u8 *d,u32 dp,volatile u8 *b,
                    int x,int y,int w,int h,int (*point)(void *),void *v)
{
    (void)c;(void)d;(void)b;(void)x;(void)y;
    CHECK(cp==640 && dp==640 && w==640 && h==480 && prod.display.active && !hal_route);
    CHECK(prod_main_mask==31 && !prod_live_pages && !point(v));
    hal_copies++;prod_event(0xc0f1);return 0;
}
void geforce_display_failed(void) { CHECK(!hal_route); }
#include "geforce_return.inc"
static void prod_hal_cases(void)
{
    prod_reset();
    kmemset(&prod,0,sizeof(prod)); /* module prepare, rather than fixture prepare */
    struct geforce_platform p={.resources=&prod_boot,
        .bar0_phys=prod_boot.bars[0].base,.display_phys=prod_boot.bars[1].base};
    GeforceModuleContext ctx={&hal_backend,&hal_counters,&gfx_current_height,
        &gfx_flip_enabled,&gfx_display_page,&p};
    static u8 client[MEM_GFX_BB8_SIZE];
    CHECK(!geforce_module_init(&hal_backend,&ctx));
    hal_backend.bb_base=client;hal_backend.bb_pitch=640;
    hal_backend.prepare();CHECK(!prod_cmd_writes && !prod_mmio_calls);
    hal_backend.init();CHECK(hal_backend.probe() && hal_route==1 && !hal_safe);
    prod_order();
    CHECK(prod_trace[prod_trace_count-3]==0xc0f1 &&
          prod_trace[prod_trace_count-2]==0xce11 && prod_trace[prod_trace_count-1]==0xfac01);
    hal_backend.init();hal_backend.enter();
    CHECK(prod_cmd_writes==1 && hal_copies==1 && hal_cells==1);
    hal_backend.shutdown();CHECK(hal_safe && !hal_route);prod_exit_order();
    hal_backend.init();CHECK(prod_cmd_writes==1 && hal_copies==2 && hal_cells==2);
    CHECK(!geforce_return(&p));prod_exit_order();
    /* Stop failure must still select 00 and check decode before 98 writes. */
    hal_backend.shutdown();hal_backend.init();prod_stop_fail=prod_stop_writes+1;
    CHECK(geforce_return(&p)==-1 && !hal_route);
    /* Lost identity forbids MMIO/redraw but still tries route00. */
    prod_cfg[13][0][0]^=1;u32 redraws=prod_redraws,reads=prod_mmio_calls;
    CHECK(geforce_return(&p)==-1 && !hal_safe && !hal_route);
    CHECK(prod_mmio_calls==reads && prod_redraws==redraws);
    output("PASS P4/P5 module: prepare-only, start/copy/cell/01, fixed reinit, stop/00/decode/redraw, reboot return failure\n");
}
