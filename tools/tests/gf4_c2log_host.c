/* Real video_start -> gf_start -> F4 -> F1/F2/F3/restore, libc-free.
 * C2svc supplies PCI/IRQ/DMA/ROM-owner seams. Dependency faults are one-shot
 * so teardown executes normally. No copy of the start composition is used. */
#include <stdbool.h>
#include "gf_startlog_host.h"
enum { LOG_INJECT=-1701 };
static const char *log_inject_stage;
static bool log_cold_devinit;
static int log_same(const char *a,const char *b)
{
    if (!a || !b) return a==b;
    while (*a && *a==*b) { ++a; ++b; }
    return *a==*b;
}
static int log_gate(const char *stage)
{
    if (!log_same(log_inject_stage,stage)) return 0;
    log_inject_stage=0;
    return 1;
}
#define GF4_SERVICE_SOURCE "gf4_startlog_service.h"
#define _start c2svc_unused_start
#include "gf4_c2svc_host.c"
#undef _start

/* The same real restore implementation, with external dependency faults. */
static bool log_after_pll,log_after_lut;
#define gf2_fence_emit(...) (log_gate("restore_drain") ? LOG_INJECT : gf2_fence_emit(__VA_ARGS__))
#define gf4_route_check(...) (log_gate("mode_calc") ? LOG_INJECT : gf4_route_check(__VA_ARGS__))
#define gf4_pllsel_calc(...) (log_after_pll=true, log_gate("pllsel_calc") ? LOG_INJECT : gf4_pllsel_calc(__VA_ARGS__))
#define gf4_analog_dpms(...) (log_gate("encoder_off") ? LOG_INJECT : gf4_analog_dpms(__VA_ARGS__))
#define gf4_mode_load(...) (log_gate("mode_load") ? LOG_INJECT : gf4_mode_load(__VA_ARGS__))
#define gf4_analog_load(...) (log_gate("analog_load") ? LOG_INJECT : gf4_analog_load(__VA_ARGS__))
#define gf4_lut_load(...) (log_after_lut=true, log_gate("lut_load") ? LOG_INJECT : gf4_lut_load(__VA_ARGS__))
#define gf4_cursor_apply(...) (log_gate("cursor") ? LOG_INJECT : gf4_cursor_apply(__VA_ARGS__))
#define gf_irq_enable(...) (log_gate("heads_irq") ? LOG_INJECT : gf_irq_enable(__VA_ARGS__))
#define gf_vram_match(...) (log_gate("restore_matches") ? LOG_INJECT : gf_vram_match(__VA_ARGS__))
#include "../../hwdev/gf/display/save.c"
#undef gf_vram_match
#undef gf2_fence_emit
#undef gf4_route_check
#undef gf4_pllsel_calc
#undef gf4_analog_dpms
#undef gf4_mode_load
#undef gf4_analog_load
#undef gf4_lut_load
#undef gf4_cursor_apply
#undef gf_irq_enable

/* Synthetic ROM only: absent DCB is legal for reaching PLL/devinit errors.
 * PLL bytes follow the existing gfinit_host.c synthetic 0x21 limits. */
static u8 log_rom[GF_INIT_PLL_TABLE+GF_PLL_TBL_HDR_MIN+
                     GF_PLL_TBL_COUNT*GF_PLL_TBL_ENTRY_MIN];
static struct gf_init_context log_init;
static struct gf_init_program log_program;
static void log_put(u32 at,u32 value,u32 width)
{
    for (u32 i=0;i<width;++i) log_rom[at+i]=(u8)(value>>(8U*i));
}
static void log_rom_fixture(void)
{
    gf2_host_memset(log_rom,0,sizeof(log_rom));
    log_rom[GF_INIT_PLL_TABLE+GF_PLL_HDR_VERSION]=GF_PLL_TBL_VERSION;
    log_rom[GF_INIT_PLL_TABLE+GF_PLL_HDR_HEADER_LEN]=GF_PLL_TBL_HDR_MIN;
    log_rom[GF_INIT_PLL_TABLE+GF_PLL_HDR_ENTRY_LEN]=GF_PLL_TBL_ENTRY_MIN;
    log_rom[GF_INIT_PLL_TABLE+GF_PLL_HDR_COUNT]=GF_PLL_TBL_COUNT;
    u32 at=GF_INIT_PLL_TABLE+GF_PLL_TBL_HDR_MIN;
    log_put(at+GF_PLL_ENT_VCO1_MIN,100,2); log_put(at+GF_PLL_ENT_VCO1_MAX,500,2);
    log_put(at+GF_PLL_ENT_VCO1_INPUT_MIN,1,2); log_put(at+GF_PLL_ENT_VCO1_INPUT_MAX,27,2);
    log_rom[at+GF_PLL_ENT_VCO1_N_MIN]=1; log_rom[at+GF_PLL_ENT_VCO1_N_MAX]=255;
    log_rom[at+GF_PLL_ENT_VCO1_M_MIN]=1; log_rom[at+GF_PLL_ENT_VCO1_M_MAX]=27;
    log_rom[at+GF_PLL_ENT_MAX_P]=7; log_rom[at+GF_PLL_ENT_BIAS_P]=1;
    log_init=(struct gf_init_context){.rom=log_rom,.rom_size=sizeof(log_rom)};
    cint_prod.display.initialized=false;
}
static int log_validate(struct gf_display_context *l)
{
    if (log_gate("deadline_after_f2")) cint_host.now+=GF2_TIMEOUT_US;
    return log_gate("rom_validate") ? LOG_INJECT : svc_validate(l);
}
static int log_acquire(struct gf_display_context *l)
{
    if (log_gate("rom_acquire")) return LOG_INJECT;
    if (!l->initialized) { l->init=&log_init; l->program=&log_program; return 0; }
    return svc_acquire(l);
}
static int log_foundation_start(struct gf_display_context *l)
{
    return log_gate("foundation_start") ? LOG_INJECT : svc_foundation_start(l);
}
static i32 log_admission(void *owner,bool open)
{
    if (log_gate(open ? "admission_open" : "admission_close")) return LOG_INJECT;
    return svc_admission(owner,open);
}
static int log_clock(void *owner,u32 *now)
{
    if (log_after_pll && log_gate("mode_deadline")) return LOG_INJECT;
    return gf3_host_clock(owner,now);
}
static int log_wait(void *owner,u32 us)
{
    if (log_after_lut && log_gate("dpms")) return LOG_INJECT;
    return gf3_host_wait(owner,us);
}
static struct gf_display_owner log_owner;
static u8 log_palette[VIDEO_PALETTE_BYTES];
static struct video_device log_device;
static void log_fixture_state(bool cold)
{
    log_inject_stage=0; log_cold_devinit=false;
    svc_fixture_state(cold);
    log_after_pll=log_after_lut=false;
    cint_prod.foundation.io.timing.clock_us=log_clock;
    cint_prod.foundation.io.timing.wait_us=log_wait;
    log_owner=svc_display_owner;
    log_owner.validate=log_validate;
    log_owner.acquire=log_acquire;
    log_owner.foundation_start=log_foundation_start;
    cint_prod.display.owner=&log_owner;
    svc_links.admission=log_admission;
    log_device=(struct video_device){.ops=&gf_video_ops,.context=&cint_prod.display};
    startlog_clear();
}
static void log_fixture(void)
{
    log_fixture_state(false);
}
static int log_start(void)
{
    struct video_mode mode;
    assert(!video_mode_info(&log_device,0,&mode));
    return video_start(&log_device,mode.id,log_palette);
}
static void log_expect(const char *stage,int rc)
{
    /* Independent expected line; check count, stage, original rc and newline. */
    char actual[sizeof(startlog)];
    for (u32 i=0;i<sizeof(actual);++i) actual[i]=startlog[i];
    startlog_clear();
    kprintf(0,"[gf4] start fail stage=%s rc=%d\n",stage,rc);
    assert(startlog_equal(actual));
    assert(svc_ctx.last_rc==rc && cint_prod.display.last_rc==rc);
    startlog_clear();
}
/* Only devinit is simulated: its full fixed-ROM scripts belong to the
 * runner suite. Baseline, F5 preparation, F1/F2 and the start composition
 * are real. Complete objects_init and stop at detect, without inventing
 * a ROM route or publishing a display. */
static int log_point(void *owner)
{
    assert(owner==&cint_host);
    return 0;
}
static void test_cold_start(void)
{
    log_fixture_state(true);
    log_rom_fixture();
    cint_prod.display.io.point=log_point;
    log_init.io=cint_prod.display.io;
    log_init.started_us=cint_host.now;
    cint_prod.display.baseline_valid=false;
    log_cold_devinit=true;
    assert(svc_ctx.ch->state==GF2_OFF && !svc_ctx.ch->f);
    assert(!cint_prod.foundation.running && !cint_prod.accel.ready);
    log_inject_stage="detect";
    assert(log_start()==LOG_INJECT);
    assert(!log_inject_stage); /* F5 deadline, F2 and real objects_init completed. */
    assert(svc_ctx.ch->f==&cint_prod.foundation);
    assert(cint_prod.display.initialized && cint_prod.display.baseline_valid);
    assert(svc_releases==1 && !svc_slot && !svc_ctx.accepting);
    log_expect("detect",LOG_INJECT);
    svc_finish();
}
void _start(void)
{
    test_cold_start();
    const char *stages[]={"admission_close","rom_validate","scan_reserve",
        "objects_init","detect","surfaces","restore_drain","restore_matches","mode_calc",
        "pllsel_calc","mode_deadline","encoder_off","mode_load","analog_load","lut_load","dpms",
        "cursor","heads_irq","admission_open"};
    for (u32 i=0;i<sizeof(stages)/sizeof(stages[0]);++i) {
        log_fixture(); log_inject_stage=stages[i];
        assert(log_start()==LOG_INJECT);
        assert(!log_inject_stage);
        log_expect(stages[i],LOG_INJECT);
        if (!log_same(stages[i],"admission_close")) assert(svc_ctx.ch->state==GF2_OFF);
        svc_finish();
    }
    const char *off_stages[]={"master_enable","foundation_start","channel_start"};
    for (u32 i=0;i<sizeof(off_stages)/sizeof(off_stages[0]);++i) {
        log_fixture(); assert(!gf2_stop(svc_ctx.ch));
        log_inject_stage=off_stages[i];
        assert(log_start()==LOG_INJECT); assert(!log_inject_stage);
        log_expect(off_stages[i],LOG_INJECT); svc_finish();
    }
    log_fixture(); cint_prod.display.initialized=false; log_inject_stage="rom_acquire";
    assert(log_start()==LOG_INJECT); log_expect("rom_acquire",LOG_INJECT); svc_finish();
    const char *rom_stages[]={"dcb_parse","pll_parse","devinit","baseline"};
    for (u32 i=0;i<sizeof(rom_stages)/sizeof(rom_stages[0]);++i) {
        log_fixture(); log_rom_fixture(); log_inject_stage=rom_stages[i];
        int rc=i==1 ? GF4_EPLL : LOG_INJECT;
        assert(log_start()==rc); assert(!log_inject_stage);
        log_expect(rom_stages[i],rc); svc_finish();
    }
    log_fixture(); svc_ctx.data=0;
    assert(log_start()==GF4_EINVAL); log_expect("service_ops",GF4_EINVAL);
    svc_ctx.data=&svc_data; svc_finish();
    log_fixture(); svc_ctx.reusable=false;
    assert(log_start()==GF4_ESTATE); log_expect("service_state",GF4_ESTATE);
    svc_ctx.reusable=true; svc_finish();
    log_fixture(); svc_ctx.ch->epoch=0;
    assert(log_start()==GF4_ESTATE); log_expect("channel_state",GF4_ESTATE); svc_finish();
    log_fixture(); svc_ctx.ch=0;
    assert(log_start()==GF4_EINVAL); log_expect("service_check",GF4_EINVAL);
    svc_ctx.ch=&cint_prod.chan; svc_finish();
    log_fixture(); svc_ctx.nroutes=0;
    assert(log_start()==GF4_ENODEV); log_expect("initial_mode",GF4_ENODEV); svc_finish();
    log_fixture(); log_inject_stage="restore_check";
    assert(log_start()==GF4_ESTATE); assert(!log_inject_stage);
    log_expect("restore_check",GF4_ESTATE); svc_finish();
    /* A long pre-F3 stage no longer poisons objects_init or display restore. */
    log_fixture(); log_inject_stage="deadline_after_f2";
    assert(!log_start()); assert(startlog_equal(""));
    assert(svc_ctx.ready && svc_ctx.accepting && svc_state.head[0].enabled); svc_finish();
    log_fixture(); assert(!log_start()); assert(startlog_equal("")); svc_finish();
    gf2_host_exit(0);
}
