/* Real video_start -> gf_start -> F4 -> F1/F2/F3/restore, libc-free.
 * C2svc supplies PCI/IRQ/DMA/ROM-owner seams. Dependency faults are one-shot
 * so teardown executes normally. No copy of the start composition is used. */
#include <stdbool.h>
#include "gf_startlog_host.h"
enum { LOG_INJECT=-1701 };
static const char *log_inject_stage;
static bool log_cold_devinit;
static bool log_check_ddc;
static u32 log_expected_engine;
static bool log_probe_check;
static u32 log_probe_count;
static i32 log_detect_rc;
static i32 log_before_probe_rc;
static u32 log_port_calls;
static void log_detect_ready(void);
struct gf4_ctx;
static i32 log_detect(struct gf4_ctx *,u32);
static i32 log_probe(struct gf4_ctx *,u8,u32);
static int log_same(const char *a,const char *b)
{
    if (!a || !b) return a==b;
    while (*a && *a==*b) { ++a; ++b; }
    return *a==*b;
}
static int log_gate(const char *stage)
{
    if (log_check_ddc && log_same(stage,"detect")) log_detect_ready();
    if (!log_same(log_inject_stage,stage)) return 0;
    log_inject_stage=0;
    return 1;
}
#define GF4_SERVICE_SOURCE "gf4_startlog_service.h"
#define _start c2svc_unused_start
#include "gf4_c2svc_host.c"
#undef _start
static struct gf4_state log_detected;
static i32 log_detect(struct gf4_ctx *c,u32 dl)
{
    i32 rc=gf4_detect(c,dl);
    if (log_probe_check) {
        log_detected=*c->state;
        log_detect_rc=c->last_rc;
    }
    return rc;
}
static i32 log_probe(struct gf4_ctx *c,u8 bus,u32 dl)
{
    if (!log_probe_check) return gf4_ddc_probe(c,bus,dl);
    assert(c->state->head[0].enabled && !c->accepting);
    if (!log_probe_count) log_before_probe_rc=c->last_rc;
    assert(bus==(log_probe_count ? 3 : 2));
    assert(dl>cint_host.now);
    /* Compare the whole detect-owned prefix: modes, attribution and EDID. */
    u32 bytes=(u8 *)&c->state->head-(u8 *)c->state;
    for (u32 i=0;i<bytes;++i)
        assert(((u8 *)c->state)[i]==((u8 *)&log_detected)[i]);
    ++log_probe_count;
    return c->last_rc=bus==2 ? 0 : GF4_EDDC;
}
static i32 log_i2c(void *owner,u8 bus,struct gf4_i2c_port *out)
{
    assert(bus==2 || bus==3);
    /* Port selection succeeds; the following real DDC probe sees NACK. */
    if (++log_port_calls%2U==0) return GF4_EDDC;
    return svc_i2c(owner,bus,out);
}

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
    if (open && log_probe_check) {
        assert(log_probe_count==2 && svc_ctx.last_rc==log_before_probe_rc);
        u32 bytes=(u8 *)&svc_state.head-(u8 *)&svc_state;
        for (u32 i=0;i<bytes;++i)
            assert(((u8 *)&svc_state)[i]==((u8 *)&log_detected)[i]);
    }
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
static void log_detect_ready(void)
{
    struct gf_io *io=&cint_prod.foundation.io;
    u32 value;
    assert(!svc_index(io,SVC_CR,RECOVER_CR1F,&value,false));
    assert(value==0x57); /* Fake retains write value; hardware reads 3. */
    assert(!io->read(io->opaque,0,0x600860U,4,&value) && value==log_expected_engine);
    assert(!io->read(io->opaque,0,0x602860U,4,&value) && value==0x200U);
    log_check_ddc=false;
}
static int log_start(void)
{
    struct video_mode mode;
    assert(!video_mode_info(&log_device,0,&mode));
    int rc=video_start(&log_device,mode.id,log_palette);
    startlog_remove_mode_diagnostics();
    return rc;
}
static void test_enter_ddc_prepare(void)
{
    for (u32 reenter=0;reenter<2;++reenter) {
        log_fixture();
        if (reenter) {
            assert(!log_start());
            assert(!video_stop(&log_device));
        }
        struct gf_io *io=&cint_prod.foundation.io;
        /* Measured boot (unlocked, I2C disabled) and post-leave (locked).
         * Keep unrelated engine bits and head B as independent controls. */
        u32 lock=reenter ? 0 : 3;
        assert(!svc_index(io,SVC_CR,RECOVER_CR1F,&lock,true));
        assert(!io->write(io->opaque,0,0x600860U,4,reenter ? 0x100U : 0));
        assert(!io->write(io->opaque,0,0x602860U,4,0x200U));
        log_expected_engine=reenter ? 0x110U : 0x10U;
        log_check_ddc=true; log_inject_stage="detect";
        assert(log_start()==LOG_INJECT);
        assert(!log_check_ddc && !log_inject_stage);
        svc_finish();
    }
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
/* Emulate owner-controlled head banking for diagnostic VGA data reads. */
static int log_banked_read(void *owner,u32 bar,u32 off,u32 width,u32 *out)
{
    if ((off==SVC_SR+1U || off==SVC_SR+RECOVER_HEAD_STRIDE+1U ||
         off==SVC_CR+1U+RECOVER_HEAD_STRIDE) && svc_host.cr[0][SVC_OWNER]!=3)
        off-=off>=SVC_CR+RECOVER_HEAD_STRIDE || off==SVC_SR+RECOVER_HEAD_STRIDE+1U ? RECOVER_HEAD_STRIDE : 0;
    return svc_read(owner,bar,off,width,out);
}
static void test_detect_state(void)
{
    log_fixture();
    struct gf_io *io=&cint_prod.foundation.io;
    u32 lock=0x99,owner=1,index=0x28;
    assert(!svc_index(io,SVC_CR,RECOVER_CR1F,&lock,true));
    assert(!svc_index(io,SVC_CR,RECOVER_CR44,&owner,true));
    assert(!io->write(io->opaque,0,SVC_CR,1,index));
    svc_ctx.memory_prepare_attempted=true; svc_ctx.memory_prepare_rc=GF_SCRIPT_EIO;
    cint_prod.feature5.last_rc=GF5_EBIOS; cint_prod.feature5.source_rc=GF_SCRIPT_EIO;
    /* A DCB pair outside the legacy defaults must be observed verbatim. */
    const u8 regs[]={0x11,0x3e,0x3f,0x36,0x37,0x51,0x50};
    const u8 values[]={0x80,0x0c,0x31,0x04,0x21,0x11,0x08};
    for (u32 i=0;i<sizeof(regs);++i) {
        u32 value=values[i];
        assert(!svc_index(io,SVC_CR,regs[i],&value,true));
    }
    assert(!io->write(io->opaque,0,RECOVER_PMC_ENABLE,4,0x12345679));
    assert(!io->write(io->opaque,0,SVC_ENGINE_CTRL,4,0x10));
    assert(!io->write(io->opaque,0,SVC_ENGINE_CTRL+RECOVER_HEAD_STRIDE,4,0x1000));
    assert(!io->write(io->opaque,0,SVC_CR,1,index));
    for (u32 h=0;h<GF4_HEADS;++h) {
        u32 delta=h*RECOVER_HEAD_STRIDE, v=0x11+h;
        assert(!svc_index(io,SVC_SR+delta,SVC_CLOCK,&v,true));
        v=0x81+h; assert(!svc_index(io,SVC_CR+delta,SVC_CONTROL,&v,true));
        v=0x41+h; assert(!svc_index(io,SVC_CR+delta,SVC_RPC,&v,true));
        assert(!io->write(io->opaque,0,SVC_DACCLK+delta,4,0x12340000+h));
        assert(!io->write(io->opaque,0,SVC_FP_TG+delta,4,0x56780000+h));
    }
    assert(!io->write(io->opaque,0,SVC_CR,1,index));
    svc_ctx.dcb.routes[0].connector=7;
    svc_ctx.dcb.routes[0].i2c_index=2;
    svc_ctx.dcb.i2c[2]=(struct gf4_i2c_port){.drive=0x51,.sense=0x50};
    svc_host.cr[0][0x21]=0x31; svc_host.cr[1][0x21]=0x32;
    svc_host.ar[0][0x10]=0x51; svc_host.ar[1][0x10]=0x52;
    svc_host.index[0][3]=0x25; svc_host.index[1][3]=0x06;
    svc_host.ar_data[0]=svc_host.ar_data[1]=true;
    io->read=log_banked_read;
    svc_ctx.last_rc=GF4_EDDC;
    svc_detect_diagnostic(&svc_ctx,&cint_prod.feature5);
    assert(startlog_equal("[gf4] detect state cr1f=99 cr44=1 read_rc=0 m_attempted=1 m_rc=-1 f5_rc=-503 source_rc=-1"
        " cr11=80 cr3e=c cr3f=31 cr36=4 cr37=21 pmc=12345679 ec0=10 ec1=1000 mmio_rc=0"
        " sr01_0=11 cr17_0=81 cr1a_0=41 sr01_1=12 cr17_1=82 cr1a_1=42"
        " dacclk0=12340000 dacclk1=12340001 fptg0=56780000 fptg1=56780001\n"
        "[gf4] detect hw h=0 cr21=31 ar10=51 config=0\n"
        "[gf4] detect hw h=1 cr21=32 ar10=52 config=0\n"
        "[gf4] detect pll vpll1=0 vpll2=0 b1=0 b2=0 ext=0 pllsel=0 sel_clk=0\n"
        "[gf4] detect port conn=7 bus=2 port_rc=0 drive_cr=51 drive=11 sense_cr=50 sense=8 read_rc=0\n"));
    assert(svc_ctx.last_rc==GF4_EDDC);
    assert(svc_host.index[0][3]==0x25 && svc_host.index[1][3]==0x06);
    io->read=svc_read;
    for (u32 i=0;i<sizeof(regs);++i) assert(svc_host.cr[0][regs[i]]==values[i]);
    u32 value;
    assert(!io->read(io->opaque,0,SVC_CR,1,&value) && value==index);
    assert(!svc_index(io,SVC_CR,RECOVER_CR1F,&value,false) && value==lock);
    assert(!svc_index(io,SVC_CR,RECOVER_CR44,&value,false) && value==owner);
    svc_finish();
}
void _start(void)
{
    /* Non-default buses distinguish connectors from I2C indices. Detect
     * falls back; a later ACK must not rebuild its modes or hide its rc. */
    log_fixture();
    svc_ctx.nroutes=2;
    svc_ctx.dcb.routes[1]=svc_ctx.dcb.routes[0];
    for (u8 i=0;i<2;++i) {
        svc_ctx.dcb.routes[i].connector=i;
        svc_ctx.dcb.routes[i].i2c_index=i+2;
        svc_ctx.dcb.i2c_rc[i+2]=0;
    }
    svc_links.i2c_port=log_i2c; log_port_calls=0;
    log_probe_check=true; log_probe_count=0;
    assert(!log_start());
    assert(log_probe_count==2);
    assert(log_port_calls==4);
    assert(startlog_equal("[gf4] probe-after-mode conn=0 rc=0\n"
                          "[gf4] probe-after-mode conn=1 rc=-405\n"));
    assert(log_detect_rc==GF4_EDDC && svc_ctx.last_rc==log_detect_rc);
    log_probe_check=false; svc_finish();
    test_enter_ddc_prepare();
    test_detect_state();
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
