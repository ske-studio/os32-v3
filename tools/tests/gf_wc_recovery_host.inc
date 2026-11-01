/* Real HAL selection + WC owner + common stop + deferred shell fault landing.
 * Acquisition/CPU/MSR and device operations are models; exec/GUI functions are extracted
 * verbatim by test_geforce.py. Each scenario runs in a fresh process. */
#include "arch/x86/kernel/mtrr_wc.h"
#include "ksetjmp.h"
static int identity_ok=1, bios_wc, wc_acquires, wc_releases, wc_effective;
static unsigned long long msr_base=0x12345000ULL, msr_mask=0x43210000ULL;
static const unsigned long long old_base=0x12345000ULL, old_mask=0x43210000ULL;
static struct x86_wc_lease lease;
int x86_wc_acquire(u32 base,u32 bytes,struct x86_wc_lease *l)
{
    CHECK(base==GF_TEST_DISPLAY && bytes==0x80000 && !l->active);
    wc_acquires++;
    *l=(struct x86_wc_lease){.active=1,.owned=!bios_wc,.old_base=msr_base,.old_mask=msr_mask};
    if (!bios_wc) { msr_base=base|1;msr_mask=0xffff80800ULL; }
    wc_effective=1;
    return 1;
}
int x86_wc_release(struct x86_wc_lease *l)
{
    CHECK(!kctx_irq_depth && !kctx_exc_depth);
    if (!l->active) return 0;
    wc_releases++;
    if (l->owned) { msr_base=l->old_base;msr_mask=l->old_mask; }
    wc_effective=bios_wc;l->active=0;
    return 0;
}
int geforce_wc_start(void)
{
    if (lease.active) return 1;
    return x86_wc_acquire(GF_TEST_DISPLAY,GEFORCE_WC_BYTES,&lease);
}
void geforce_wc_report(const char *s) { (void)s; }
#include "gf_wc_stop.inc"
int gf_production_prepare(struct gf_production *p,const struct gf_production_resources *r,
                          int (*route_fn)(void *),void *o)
{ (void)r;(void)route_fn;(void)o;p->video=device_gf;p->prepared=true;return 0; }
int gf_production_identity(struct gf_production *p) { (void)p;return identity_ok ? 0 : -1; }
int gf_production_decode_safe(struct gf_production *p) { (void)p;return 0; }
int gf_production_quiesce(struct gf_production *p)
{ CHECK(!lease.active);return gf_display_stop(p->video.context); }
int gf_production_point(void *p) { (void)p;return identity_ok ? 0 : -1; }
int pc98_route_select_98(void) { return pc98_route_to_98(); }
#define geforce_backend_ops gfx_backend_geforce
#include "geforce_body.inc"
#undef geforce_backend_ops

static struct ledger_region recovery_regions[LEDGER_MAX_REGIONS];
static struct ledger_resource recovery_resources[LEDGER_MAX_RESOURCES];
static u32 recovery_ptes[GF_PRODUCTION_BAR0_BYTES/PAGE_SIZE+MEM_GFX_BB8_SIZE/PAGE_SIZE];
static void wc_snapshot(void)
{
    __builtin_memcpy(recovery_regions,ledger_regions,sizeof(recovery_regions));
    __builtin_memcpy(recovery_resources,ledger_resources,sizeof(recovery_resources));
    for (u32 i=0;i<sizeof(recovery_ptes)/sizeof(u32);i++) {
        u32 va=i<GF_PRODUCTION_BAR0_BYTES/PAGE_SIZE ? GF_TEST_BAR0+i*PAGE_SIZE :
            GF_TEST_DISPLAY+(i-GF_PRODUCTION_BAR0_BYTES/PAGE_SIZE)*PAGE_SIZE;
        recovery_ptes[i]=paging_pte_raw(va);
    }
}
static void wc_restored(void)
{
    CHECK(!lease.active && wc_effective==bios_wc);
    CHECK(msr_base==old_base && msr_mask==old_mask);
    CHECK(wc_releases==wc_acquires && !route && console_safe);
    for (u32 i=0;i<sizeof(recovery_regions);i++)
        CHECK(((u8 *)ledger_regions)[i]==((u8 *)recovery_regions)[i]);
    for (u32 i=0;i<sizeof(recovery_resources);i++)
        CHECK(((u8 *)ledger_resources)[i]==((u8 *)recovery_resources)[i]);
    for (u32 i=0;i<sizeof(recovery_ptes)/sizeof(u32);i++) {
        u32 va=i<GF_PRODUCTION_BAR0_BYTES/PAGE_SIZE ? GF_TEST_BAR0+i*PAGE_SIZE :
            GF_TEST_DISPLAY+(i-GF_PRODUCTION_BAR0_BYTES/PAGE_SIZE)*PAGE_SIZE;
        CHECK(recovery_ptes[i]==paging_pte_raw(va));
    }
}

/* Unrelated owner resources are inert. The real notify sequence still calls
 * gui_owner_exit; no fixture directly calls shutdown or the stop callback. */
static int g_last_kind,g_last_code,exec_exit_status,exec_nest_level;
static u32 g_exit_jmpbuf[KSETJMP_BUF_LEN];
static AppSlot *g_cur_app;
volatile u32 kctx_irq_depth,kctx_exc_depth;
struct v86_session_state v86_session;
static int font_guard_active;
static void font_guard_fault(int a,int b,int c) { (void)a;(void)b;(void)c;CHECK(0); }
static void exec_stop_mark(void) { CHECK(0); }
void v86_session_end(void) {}
static void v86_call_cleanup(void) { CHECK(!kctx_irq_depth && !kctx_exc_depth); }
static void ring3_context_clear(void) { host_user=0; }
static void res_owner_set(int id) { host_owner=id; }
static void exec_reclaim_resources(int id) { CHECK(id==APP_ID_SHELL); }
static void shell_service_retire(void) {}
static void exec_teardown_app(AppSlot *a) { (void)a;CHECK(0); }
static void exec_restore_context(int id) { (void)id;CHECK(0); }
int appslot_return_target(int id) { (void)id;CHECK(0);return 0; }
u32 appslot_reclaim(int id) { (void)id;CHECK(0);return 0; }
void appslot_switch_to(int id) { (void)id;CHECK(0); }
static void input_discard_stop(void) {}
static void ime_owner_exit(int id) { (void)id; }
static void kbd_inject_owner_exit(int id) { (void)id; }
static int con_sink_reader_get(void) { return 0; }
static void con_sink_push_exit(int id) { (void)id;CHECK(0); }
static int launch_child(int id) { (void)id;return 0; }
static void kbd_inject_discard(void) {}
static void launch_owner_exit(int id) { (void)id; }
static void host_owner_exit(int id) { (void)id; }
static void con_sink_owner_exit(int id) { (void)id; }
void appslot_gfx_owner_exit(int id) { (void)id; }
static void *g_gui_pump;
static int ime_reset,gui_input;
void input_set_gui_mode(int on) { gui_input=on; }
void ime_set_render(void *p) { CHECK(!p);ime_reset++; }
#include "gf_wc_fault_path.inc"
static i32 shell_notice(u32 op,u32 arg,int owner)
{ CHECK(op==GUI_OP_OWNER_EXIT && owner==APP_ID_SHELL && arg==EXEC_KIND_FAULT);return 0; }
static void shell_fault(void)
{
    int released=wc_releases;
    host_cur=host_owner=APP_ID_SHELL;host_user=0;
    AppSlot *a=&host_slots[APP_ID_SHELL];
    g_gui_handler=shell_notice;g_gui_pump=(void *)shell_notice;gui_input=1;
    if (!exec_setjmp(a->jmpbuf)) {
        kctx_exc_depth=1;
        exec_fault_recover();
        CHECK(0);
    }
    /* The real assembly longjmp restores the ordinary-context depths. */
    CHECK(!kctx_exc_depth && !kctx_irq_depth && lease.active && wc_releases==released);
    exec_pending_finish();
    CHECK(exec_exit_status==EXEC_ERR_FAULT && g_last_kind==EXEC_KIND_FAULT);
    CHECK(!g_gui_handler && !g_gui_pump && !gui_input && ime_reset);
    wc_restored();
}
static void wc_recovery_run(void)
{
    map_host(0x10000,0xff0000);map_host(GF_TEST_DISPLAY,MEM_GFX_BB8_SIZE);
    boot();gfx_set_backend_pref(GFX_PREF_GEFORCE);gfx_boot_reserve();
    struct geforce_platform *p=geforce_platform_get();
    GeforceModuleContext ctx={&gfx_backend_geforce,&gfx_counters,&gfx_current_height,
                             &gfx_flip_enabled,&gfx_display_page,p};
    CHECK(!geforce_module_init(&gfx_backend_geforce,&ctx));
    wc_snapshot();
    bios_wc=BIOS_WC;wc_effective=bios_wc;host_user=0;host_gui=1;
    gfx_init();CHECK(lease.active && wc_acquires==1 && route);
    gfx_init_200();CHECK(lease.active && wc_acquires==1);
    if (RECOVERY_PATH<2) {
        identity_ok=0;
        if (RECOVERY_PATH==0) gfx_init_200();else gfx_init();
        wc_restored();
        CHECK(g_backend==&gfx_backend_pc98);
    } else {
        for (int n=0;n<3;n++) {
            if (n) { gfx_init();CHECK(lease.active && route); }
            shell_fault();
            CHECK(gfx_current_height==GFX_HEIGHT && !gfx_flip_enabled);
            /* CUI after the third fault has no remaining GF WC lease. */
        }
    }
    SAY("PASS WC recovery: real reinit/shell fault path restores MTRR and lease");die(0);
}
