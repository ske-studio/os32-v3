/* Ownerless platform adapter + real P4 owner/transport/runner, fake ports/copy.
 * F4 module initialization is covered separately by gf4_c3_host.c. */
#include "drivers/video/geforce/geforce_module.h"
#include "gfx/palette.h"
#define production prod
#include "gf_ownerless_body.inc"
#undef production
struct gf1_test_ops gf1_module_ops;
void gf2_test_bind_production(struct gf_foundation *f,struct gf2_chan *ch)
{ (void)f;(void)ch; }
/* Optional F4 HAL entries are retained by the backend ops table. This
 * ownerless fixture never invokes them: fail at runtime if that changes. */
int gf2_fence_emit(struct gf2_chan *ch,struct gf2_fence *out,u32 dl)
{ (void)ch;(void)out;(void)dl;__builtin_trap(); }
int gf2_fence_wait(struct gf2_chan *ch,struct gf2_fence fence,u32 dl)
{ (void)ch;(void)fence;(void)dl;__builtin_trap(); }
int gf_vram_next(struct gf2_chan *ch,u32 *cursor,struct gf_vram_reservation *out)
{ (void)ch;(void)cursor;(void)out;__builtin_trap(); }
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
/* The leave diagnostic is tested by gf4_scanlog_host; this harness only needs the return order. */
static void geforce_leave_log(struct gf_io *io,int stop_rc) { (void)io;(void)stop_rc; }
#include "geforce_return.inc"
/* Run the real boot adapter before module prepare. The paging double records
 * the full BAR1 tail; test_geforce exercises these calls with real PTs. */
static u32 boot_maps, boot_closes;
int ledger_resource_add(const struct ledger_resource *r,u32 *id)
{ *id=r->bar;return 1; }
int ledger_reserve_set(u32 owner,const struct ledger_span *spans,u32 n)
{ CHECK(owner==LEDGER_OWNER_GFX && n==RECOVER_BAR_COUNT && spans);return 1; }
int ledger_resource_set_map(u32 id,u32 first,u32 end)
{ CHECK(id<RECOVER_BAR_COUNT && first<end);return 1; }
int paging_map_phys(u32 va,u32 pa,u32 pages,u32 flags)
{
    CHECK(va==pa && flags==(PAGE_RW|PTE_PCD));
    if (boot_maps==3) {
        CHECK(va==prod_boot.bars[1].base+PAGE_ALIGN_UP(GF_MODE_VRAM_BYTES));
        CHECK(pages==(prod_boot.bars[1].length-PAGE_ALIGN_UP(GF_MODE_VRAM_BYTES))/PAGE_SIZE);
    }
    boot_maps++;return 0;
}
int paging_set_not_present(u32 first,u32 last)
{
    CHECK(first<last);
    if (boot_closes==1) CHECK(last==prod_boot.bars[1].base+prod_boot.bars[1].length-1);
    boot_closes++;return 0;
}
int geforce_wc_start(void) { return 0; }
int geforce_wc_stop(void) { return 0; }
void geforce_wc_report(const char *s) { (void)s; }
#include "../../drivers/video/geforce/geforce_platform.c"
static void prod_hal_cases(void)
{
    prod_reset();
    kmemset(&prod,0,sizeof(prod)); /* module prepare, rather than fixture prepare */
    /* A cold BAR0 is readable but zero, including PFB and vs. */
    unsigned long map_args[6]={prod_boot.bars[0].base,prod_boot.bars[0].length,
                               3,0x32,(unsigned long)-1,0};
    unsigned long mapped;
    __asm__ volatile("int $0x80":"=a"(mapped):"a"(90),"b"(map_args):"memory");
    CHECK(mapped==prod_boot.bars[0].base);
    CHECK(!*(volatile u32 *)(mapped+GF_VRAM_SIZE_REG));
    prod_pfb=0;
    gf_production_configure("geforce",0);
    struct geforce_platform *p=geforce_platform_get();
    CHECK(p && p->reserve && p->reserve());
    CHECK(boot_maps==4 && boot_closes==2 && p->resources);
    CHECK(!prod.prepared);
    const struct gf3_test_ops *accel=0;
    const struct gf1_test_ops *foundation=0;
    GeforceModuleContext ctx={.backend=&hal_backend,.counters=&hal_counters,
        .height=&gfx_current_height,.flip=&gfx_flip_enabled,.page=&gfx_display_page,
        .platform=p,.accel_out=&accel,.foundation_out=&foundation};
    static u8 client[MEM_GFX_BB8_SIZE];
    CHECK(!gf_ownerless_module_init(&hal_backend,&ctx));
    CHECK(prod.prepared && prod.display.io.read && prod.foundation.io.timing.clock_us);
    prod_pfb=0x10000000;
    output("PASS cold boot adapter -> module init -> production prepare, complete BAR1 tail PT calls\n");
    CHECK(!prod.display.display4 && !gf3_service_get(&prod.chan));
    hal_backend.bb_base=client;hal_backend.bb_pitch=640;
    hal_backend.prepare();CHECK(!prod_cmd_writes && prod_mmio_calls==2);
    hal_backend.init();CHECK(hal_backend.probe() && hal_route==1 && !hal_safe);
    prod_order();
    CHECK(prod_trace[prod_trace_count-3]==0xc0f1 &&
          prod_trace[prod_trace_count-2]==0xce11 && prod_trace[prod_trace_count-1]==0xfac01);
    hal_backend.init();hal_backend.enter();
    CHECK(prod_cmd_writes==2 && hal_copies==1 && hal_cells==1);
    hal_backend.shutdown();CHECK(hal_safe && !hal_route);prod_exit_order();
    hal_backend.init();CHECK(prod_cmd_writes==3 && hal_copies==2 && hal_cells==2);
    CHECK(!geforce_return(p));prod_exit_order();
    /* Stop failure must still select 00 and check decode before 98 writes. */
    hal_backend.shutdown();hal_backend.init();prod_stop_fail=prod_stop_writes+1;
    CHECK(geforce_return(p)==-1 && !hal_route);
    /* Lost identity forbids MMIO/redraw but still tries route00. */
    prod_cfg[13][0][0]^=1;u32 redraws=prod_redraws,reads=prod_mmio_calls;
    CHECK(geforce_return(p)==-1 && !hal_safe && !hal_route);
    CHECK(prod_mmio_calls==reads && prod_redraws==redraws);
    output("PASS P4/P5 ownerless platform: prepare-only, start/copy/cell/01, fixed reinit, stop/00/decode/redraw, reboot return failure\n");
}
