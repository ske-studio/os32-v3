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
#include "../../hwdev/gf/command.h"
#include "../../hwdev/gf/vram_view.h"
/* This HAL fixture has no command owner. Unexpected layer-2 use must fail. */
int gf2_start(struct gf2_chan *ch, struct gf_foundation *f)
{ (void)ch;(void)f;return GF2_ESTATE; }
/* Run the actual P3 start state machine. These seams replace lower hardware
 * operations, not the acquire-once/validate-on-resume decision. */
static int acquire_gf(struct gf_display_context *c) { (void)c; acquires++; return INIT_FAIL ? -1 : 0; }
static void release_gf(struct gf_display_context *c) { (void)c; releases++; }
static int validate_gf(struct gf_display_context *c) { (void)c; validates++; return 0; }
static int foundation_gf(struct gf_display_context *c) { (void)c;return 0; }
static const struct gf_display_owner owner_gf={
    .acquire=acquire_gf, .release=release_gf, .validate=validate_gf,
    .foundation_start=foundation_gf,.foundation_stop=foundation_gf
};
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
static int host_gf_selected=1;
static u32 host_gf_probes;
int gf_production_selected(void) { return host_gf_selected; }
int gf_production_identify(struct gf_production_resources *r)
{
    host_gf_probes++;
    *r=(struct gf_production_resources){0};
    r->identity.device=RECOVER_GF_DEVICE;
    r->bars[0]=(struct recover_bar_extent){GF_TEST_BAR0,GF_TEST_BAR0,GF_PRODUCTION_BAR0_BYTES,1};
    r->bars[1]=(struct recover_bar_extent){GF_TEST_DISPLAY|8,GF_TEST_DISPLAY,GF_PRODUCTION_BAR1_BYTES,1};
    r->bars[2]=(struct recover_bar_extent){GF_TEST_BAR2,GF_TEST_BAR2,GF_PRODUCTION_BAR2_BYTES,1};
    return 0;
}
int geforce_wc_start(void) { return 0; }
int geforce_wc_stop(void) { return 0; }
void geforce_wc_report(const char *s) { (void)s; }
#include "../../drivers/video/geforce/geforce_platform.c"
#include "geforce_reserve.inc"
static void __attribute__((unused)) foundation_selection_test(void)
{
    u32 probes=host_gf_probes;
    host_gf_selected=0;
    CHECK(!geforce_platform_get() && !reserve() && host_gf_probes==probes);
    CHECK(!geforce_boot_reserve() && host_gf_probes==probes);
    CHECK(!ledger_surface_find(LEDGER_SF_GEFORCE,LEDGER_ROLE_CLIENT));
    CHECK(!ledger_surface_find(LEDGER_SF_GEFORCE,LEDGER_ROLE_DISPLAY));
    host_gf_selected=1;
}
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

#ifdef EXPECT_VRAM_PT
/* Real paging + production adapters: a live AS forbids creating new PTs. */
static struct gf_foundation vram_foundation;
static struct gf2_chan vram_channel;
static i32 close_test_view(void *owner, struct gf_vram_view *view)
{ (void)owner; return gf_vram_view_unmap(view); }
static void prepared_vram_test(void)
{
    const u32 v = gf_vram_bytes(TEST_VRAM_BYTES);
    const u32 r = gf_instmem_reserved(TEST_VS_MASK);
    const u32 push = gf_push_base(v-r);
    const u32 display_end = PAGE_ALIGN_UP(GF_MODE_VRAM_BYTES);
    vram_foundation.imem.vram_bytes=v;
    vram_foundation.imem.reserved=r;
    vram_channel=(struct gf2_chan){
        .bar1={GF_TEST_DISPLAY|8,GF_TEST_DISPLAY,GF_PRODUCTION_BAR1_BYTES,1},
        .map_ops=&gf_production_instmem_map_ops
    };
    vram_channel.f=&vram_foundation;
    vram_channel.B=push;
    u32 owner;
    CHECK(ledger_owner_new(LEDGER_KIND_AS,2,"gf-vram-pt",&owner));
    CHECK(!paging_addrspace_create_lease(&space,owner));
    if (!EXPECT_VRAM_PT) {
        /* Same boot function with only BAR1 preparation removed. Ledger and
         * production_map are real, so EMAP comes from the missing live-AS PT. */
        volatile u32 *va;
        CHECK(!paging_registered_pt(GF_TEST_DISPLAY+push));
        CHECK(!geforce_range_reserved(GF_TEST_DISPLAY+push,GF2_PUSH_BYTES));
        CHECK(gf2_map_vram(&vram_channel,push,GF2_PUSH_BYTES,&va)==GF2_EMAP);
        CHECK(!gf_vram_registry_init(&vram_channel));
        struct gf_vram_reservation_node node={0};
        const u32 off=MEM_DEVICE_APERTURE_PDE_SIZE;
        CHECK(!gf_vram_reserve(&vram_channel,&node,off,PAGE_SIZE,&node));
        struct gf_vram_view view={.close=close_test_view};
        volatile u8 *ptr;
        CHECK(gf_vram_view_map(&view,&vram_channel,off,PAGE_SIZE,&ptr)==GF2_EMAP);
        CHECK(!gf_vram_release(&vram_channel,&node.value));
        paging_addrspace_destroy(&space);
        CHECK(ledger_owner_retire(owner));
        return;
    }
    /* Check every closed PTE, including both ends of the push aperture. */
    for (u32 off=display_end;off<push+GF2_PUSH_BYTES;off+=PAGE_SIZE) {
        u32 va=GF_TEST_DISPLAY+off;
        CHECK(paging_pte_raw(va)==(va|PTE_RW|PTE_PCD));
        u32 pde=((u32 *)P2V(space.pd_phys))[va/MEM_DEVICE_APERTURE_PDE_SIZE];
        CHECK((pde&PTE_FRAME_MASK)==paging_registered_pt(va) && (pde&PTE_PRESENT));
    }
    CHECK(!paging_pte_raw(GF_TEST_DISPLAY+push+GF2_PUSH_BYTES));
    volatile u32 *va;
    CHECK(!gf2_map_vram(&vram_channel,push,GF2_PUSH_BYTES,&va));
    CHECK((paging_pte_raw(GF_TEST_DISPLAY+push)&(PTE_PRESENT|PTE_RW|PTE_USER|PTE_PCD))==
          (PTE_PRESENT|PTE_RW|PTE_PCD));
    CHECK(!gf2_unmap_vram(&vram_channel));
    CHECK(!gf_vram_registry_init(&vram_channel));
    struct gf_vram_reservation_node node={0};
    CHECK(!gf_vram_reserve(&vram_channel,&node,display_end,push-display_end,&node));
    /* First view, a different PDE, and the highest available page. */
    const u32 offsets[]={display_end,MEM_DEVICE_APERTURE_PDE_SIZE-PAGE_SIZE,
                        MEM_DEVICE_APERTURE_PDE_SIZE,push-PAGE_SIZE};
    for (u32 i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        struct gf_vram_view view={.close=close_test_view};
        volatile u8 *ptr;
        CHECK(!gf_vram_view_map(&view,&vram_channel,offsets[i],PAGE_SIZE,&ptr));
        CHECK((paging_pte_raw(GF_TEST_DISPLAY+offsets[i])&(PTE_PRESENT|PTE_USER|PTE_PCD))==
              (PTE_PRESENT|PTE_PCD));
        CHECK(!gf_vram_view_unmap(&view));
    }
    CHECK(!gf_vram_release(&vram_channel,&node.value));
    CHECK(paging_pte_raw(GF_TEST_DISPLAY)&PTE_PRESENT);
    paging_addrspace_destroy(&space);
    CHECK(ledger_owner_retire(owner));
}
#endif
