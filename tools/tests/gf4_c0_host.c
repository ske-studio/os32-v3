/* UNIT 13: real C0-load + C0-tx; all lower dependencies come from A1.
 * Register oracle: MIT nouveau c03c1a1 dispnv04/hw.c:468-535,665-835,
 * hw.h:70-124; crtc.c:461-606. This fixture does not read external sources.
 * Link with GF_PRODUCTION_MODULE and function/data sections + --gc-sections;
 * gf_load_state.c supplies the shared descriptor order. */
#define GF4_HOST_UNIT 13
#include "gf4_display_fake.h"
#include "../../hwdev/gf/internal.h"
#define REQUIRE(x) do { __asm__ volatile ("" ::: "memory"); if (!(x)) return __LINE__; } while (0)
/* hw.h:70-124 / crtc.c:91-102: independent expected register addresses. */
/* crtc.c:334-337: SR1 CLKDIV2 and character-clock bit. */
#define T_SR_CLOCK 1U
#define T_SR_DIV2 0x08U
#define T_SR_RUN 0x01U
#define T_SR_OFF 0x20U
/* tvnv17.h:54-70: named fixture ordinals, independent of C0 constants. */
enum t_tv_norm { T_PAL, T_PAL_M, T_PAL_N, T_PAL_NC, T_NTSC_M, T_NTSC_J,
    T_HD480I, T_HD480P, T_HD576I, T_HD576P, T_HD720P, T_HD1080I };
/* tvmodesnv17.c:439-443, CRTC vertical normalization per crtc.c:245-256. */
#define T_LD_WIDTH 320U
#define T_LD_HEIGHT 200U
#define T_LD_SCAN_FACTOR 2U
#define T_LD_CLOCK 90000U /* tvnv17.c:363 */
#define T_SD_CLOCK 27000U /* tvmodesnv17.c:150,166 */
#define T_HD_CLOCK 74250U /* tvmodesnv17.c:182,198 */
#define T_HEAD_B 1U
#define T_ENGINE_B 0x602860U
#define T_LCD_B 0x6033d4U
#define T_BASE_B 0x602800U
#define T_PLLSEL 0x68050cU
#define T_SELCLK 0x680524U
#define T_GATE 0xc040U
#define T_STAGE 0x680580U
#define T_COEFF_A 0x680578U
#define T_COEFF_B 0x68057cU
#define T_VPLL_A 0x680508U
#define T_VPLL_B 0x680520U
#define T_PLL_MASK ((1U<<28)|(1U<<9)|(1U<<29)|(1U<<11)|(1U<<2))
#define T_GATE_A (3U<<16)
#define T_GATE_B (3U<<18)
#define T_SINGLE_A (1U<<8)
#define T_SINGLE_B (1U<<28)
#define T_GATE_SEED 0xa5a55a5aU /* deliberately distinct unrelated bits */
#define T_SHARED_SEED 0x90000000U
#define T_OWNER_SEED 3U
#define T_COEFF_VALUE 0x1234U /* arbitrary PLL solution sentinel, not a clock oracle */
#define T_VPLL_VALUE 0x201U
#define T_VPLL_KEEP 0xfff80000U /* nvkm/subdev/devinit/nv04.c:198-273 */
#define T_LCD_SEED 0x12U
#define T_REGISTERS 256U /* sparse model, enough for both descriptor loads */
/* hw.c:794-807: DAC data count; sized to hold normal + rollback full loads. */
#define T_EVENTS (16U * GF4_HOST_LUT_BYTES)
#define T_DEADLINE GF3_TIMEOUT_US
#define T_OLD_ID 17U
#define T_NEW_ID 18U
#define T_OFF_OLD GF_MODE_VRAM_BYTES
#define T_OFF_NEW (3U * GF_MODE_VRAM_BYTES)
#define T_FAILURE GF2_EIO
static struct gf4_host host;
static struct gf4_state state;
static struct gf_display_context legacy;
static struct gf4_host_event events[T_EVENTS];
static struct gf3_host_reg registers[T_REGISTERS];
static void setup(void)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=T_EVENTS;
    host.base.regs=registers; host.base.cregs=T_REGISTERS;
    gf2_host_memset(&legacy,0,sizeof(legacy));
    legacy.io=host.base.f.io; legacy.baseline.sel_clk=T_SHARED_SEED;
    host.ctx.legacy=&legacy;
    state.nmodes=1;
    state.modes[0]=(struct gf4_mode){.id=T_NEW_ID,.fmt=GF3_INDEX8,.pitch=GF_MODE_WIDTH,
        .t={GF_MODE_CLOCK_KHZ,GF_MODE_WIDTH,GF_MODE_HSYNC_START,GF_MODE_HSYNC_END,GF_MODE_HTOTAL,
            GF_MODE_HEIGHT,GF_MODE_VSYNC_START,GF_MODE_VSYNC_END,GF_MODE_VTOTAL,GF4_NEG_H|GF4_NEG_V}};
    for (u8 i=0;i<GF4_HEADS;i++) {
        host.ctx.limits_valid[i]=true;
        struct gf4_head_image *h=&state.head[i];
        h->enabled=true; h->mode=state.modes[0]; h->mode.id=T_OLD_ID;
        h->route=(struct gf4_route){.head=i,.head_mask=3,.or_mask=1U<<i,.type=GF4_ANALOG};
        h->front=(struct gf3_surface){.fmt=GF3_INDEX8,.off=T_OFF_OLD+i*GF_MODE_VRAM_BYTES,
            .bytes=GF_MODE_VRAM_BYTES,.pitch=GF_MODE_WIDTH,.width=GF_MODE_WIDTH,.height=GF_MODE_HEIGHT};
        h->dpms=GF4_ON; h->cursor_visible=h->cursor_valid=true;
        struct gfinit_mode *m=&state.work[i];
        m->sr[T_SR_CLOCK]=GF_MODE_SR_BLANKED;
        m->head=i; m->pllsel=T_PLL_MASK; m->sel_clk=T_SHARED_SEED;
        m->engine_ctrl=i ? 0 : GF_MODE_ENGINE_I2C;
        m->cr[GF_MODE_CR_LCD]=T_LCD_SEED; /* sentinel for ext ordering */
        m->start=h->front.off;
        m->sequence.count=5; /* nv04.c:198-273: gate, stage, coeff, VPLL, gate */
        m->sequence.writes[0]=(struct gfinit_pll_write){T_GATE,T_GATE,~(i ? T_GATE_B : T_GATE_A),0};
        m->sequence.writes[1]=(struct gfinit_pll_write){T_STAGE,T_STAGE,~(i ? T_SINGLE_B : T_SINGLE_A),i ? T_SINGLE_B : T_SINGLE_A};
        m->sequence.writes[2]=(struct gfinit_pll_write){i ? T_COEFF_B : T_COEFF_A,0,0,T_COEFF_VALUE};
        m->sequence.writes[3]=(struct gfinit_pll_write){i ? T_VPLL_B : T_VPLL_A,i ? T_VPLL_B : T_VPLL_A,T_VPLL_KEEP,T_VPLL_VALUE};
        m->sequence.writes[4]=(struct gfinit_pll_write){T_GATE,T_GATE,~0U,0};
        state.committed[i]=*m;
        state.tv[i].version=GF4_TV_VERSION;
    }
    host.base.f.wanted_heads=host.base.ch.heads=3;
    gf3_host_set(&host.base,0,T_GATE,T_GATE_SEED);
    host.cr[0][0x44]=T_OWNER_SEED;
}
static int write_index(u32 off,u32 value)
{
    for (u32 i=0;i<host.nevents;i++) {
        const struct gf4_host_event *e=&events[i];
        if (e->op==D_WRITE && e->a[1]==off && e->a[3]==value) return i;
    }
    return -1;
}
static int load_and_commit(void)
{
    setup();
    struct gf4_route route=state.head[T_HEAD_B].route;
    struct gf3_surface front=state.head[T_HEAD_B].front;
    front.off=T_OFF_NEW; state.work[T_HEAD_B].start=front.off;
    REQUIRE(!gf4_set_mode(&host.ctx,&route,T_NEW_ID,&front,T_DEADLINE));
    REQUIRE(state.head[T_HEAD_B].mode.id==T_NEW_ID);
    REQUIRE(state.head[T_HEAD_B].front.off==T_OFF_NEW);
    REQUIRE(state.head[0].mode.id==T_OLD_ID && state.head[0].front.off==T_OFF_OLD);
    REQUIRE(host.ctx.accepting && !host.ctx.changing && host.ctx.reusable);
    REQUIRE(host.ctx.epoch==host.base.ch.epoch && !host.ctx.recovery_rc);
    REQUIRE(host.calls[D_FENCE]==1 && host.calls[D_FENCE_WAIT]==1);
    REQUIRE(host.calls[D_CHANGE_BEGIN]==0 && host.calls[D_CHANGE_END]==0);
    REQUIRE(write_index(T_COEFF_B,T_COEFF_VALUE)>=0);
    REQUIRE(write_index(T_GATE,T_GATE_SEED)>=0);
    REQUIRE(write_index(T_PLLSEL,T_PLL_MASK)>=0 && write_index(T_SELCLK,T_SHARED_SEED)>=0);
    REQUIRE(write_index(T_BASE_B,T_OFF_NEW)>=0);
    int engine=write_index(T_ENGINE_B,0),lcd=write_index(T_LCD_B,GF_MODE_CR_LCD);
    REQUIRE(engine>=0 && lcd>engine);
    REQUIRE(host.cr[0][0x44]==T_OWNER_SEED);
    REQUIRE(host.calls[D_LUT]==1);
    REQUIRE(events[host.nevents-1].op==D_ADMISSION && events[host.nevents-1].a[0]);
    return 0;
}
static int failures(void)
{
    setup();
    gf4_host_fail_at(&host,D_SURFACE,1,GF4_ERANGE);
    struct gf4_route route=state.head[T_HEAD_B].route;
    struct gf3_surface front=state.head[T_HEAD_B].front;
    REQUIRE(gf4_set_mode(&host.ctx,&route,T_NEW_ID,&front,T_DEADLINE)==GF4_ERANGE);
    REQUIRE(!host.calls[D_ADMISSION] && !host.calls[D_WRITE]);
    setup();
    gf4_host_fail_at(&host,D_FENCE_WAIT,1,T_FAILURE);
    REQUIRE(gf4_change_begin(&host.ctx,T_DEADLINE)==T_FAILURE);
    REQUIRE(host.ctx.accepting && !host.ctx.changing);
    REQUIRE(!host.calls[D_CURSOR_APPLY] && !host.calls[D_WRITE]);
    setup();
    gf4_host_fail_at(&host,D_CURSOR_APPLY,2,T_FAILURE);
    REQUIRE(gf4_change_begin(&host.ctx,T_DEADLINE)==T_FAILURE);
    REQUIRE(host.ctx.accepting && !host.ctx.changing && !host.calls[D_WRITE]);
    REQUIRE(host.calls[D_CURSOR_APPLY]==2U*GF4_HEADS);
    setup();
    REQUIRE(!gf4_change_begin(&host.ctx,T_DEADLINE));
    u32 count=host.nevents;
    REQUIRE(gf4_change_begin(&host.ctx,T_DEADLINE)==GF4_EBUSY && host.nevents==count);
    state.next[T_HEAD_B].mode.id=T_NEW_ID;
    REQUIRE(gf4_change_end(&host.ctx,T_FAILURE)==T_FAILURE);
    REQUIRE(state.head[T_HEAD_B].mode.id==T_OLD_ID && host.ctx.accepting);
    REQUIRE(!host.ctx.recovery_rc && host.ctx.reusable && !host.calls[D_FAILED]);
    REQUIRE(state.committed[T_HEAD_B].pllsel==T_PLL_MASK);
    REQUIRE(host.ctx.epoch==host.base.ch.epoch);
    setup();
    REQUIRE(!gf4_change_begin(&host.ctx,T_DEADLINE));
    gf4_host_fail_at(&host,D_LUT,1,GF4_ETIME);
    REQUIRE(gf4_change_end(&host.ctx,T_FAILURE)==T_FAILURE);
    REQUIRE(host.ctx.recovery_rc==GF4_ETIME && !host.ctx.reusable && !host.ctx.accepting);
    REQUIRE(host.calls[D_FAILED]==1 && state.head[T_HEAD_B].mode.id==T_OLD_ID);
    REQUIRE(state.committed[T_HEAD_B].pllsel==T_PLL_MASK);
    REQUIRE(state.head[T_HEAD_B].front.off==T_OFF_OLD+GF_MODE_VRAM_BYTES);
    return 0;
}
static int preparation_and_io(void)
{
    setup();
    struct gfinit_mode output={.start=T_OFF_NEW};
    gf4_host_fail_at(&host,D_FORMAT,1,T_FAILURE);
    REQUIRE(gf4_mode_calc(&state.modes[0],&state.head[T_HEAD_B].route,
        &legacy.baseline,&host.ctx.limits[T_HEAD_B],&state.head[T_HEAD_B].front,&output)==T_FAILURE);
    REQUIRE(output.start==T_OFF_NEW && !host.calls[D_WRITE]);
    setup();
    host.ctx.hold[T_HEAD_B]=(struct gf4_flip_hold){.pending=true,.base_written=true,
        .old_front=state.head[T_HEAD_B].front,.new_front=state.head[0].front};
    gf4_host_fail_at(&host,D_FLIP_COMPLETE,1,T_FAILURE);
    REQUIRE(gf4_change_begin(&host.ctx,T_DEADLINE)==T_FAILURE);
    REQUIRE(host.ctx.hold[T_HEAD_B].pending && host.ctx.hold[T_HEAD_B].base_written);
    REQUIRE(!host.calls[D_FENCE] && !host.calls[D_WRITE]);
    setup();
    REQUIRE(!gf4_change_begin(&host.ctx,T_DEADLINE));
    gf4_host_trace_clear(&host);
    /* Fail after owner capture/selection: cleanup must preserve the bus rc
     * while restoring the original primary CR44 selection. */
    gf4_host_fail_at(&host,D_WRITE,8,T_FAILURE);
    REQUIRE(gf4_mode_load(&host.ctx,T_HEAD_B,&state.work[T_HEAD_B],T_DEADLINE)==T_FAILURE);
    REQUIRE(host.cr[0][0x44]==T_OWNER_SEED);
    gf4_host_fail_at(&host,D_WRITE,0,0);
    REQUIRE(gf4_change_end(&host.ctx,T_FAILURE)==T_FAILURE);
    REQUIRE(!host.ctx.recovery_rc && host.ctx.accepting);
    setup();
    host.ctx.saved_closed=true;
    host.ctx.diagnostic_saved=&state.rollback;
    host.ctx.accepting=false;
    REQUIRE(!gf4_change_begin(&host.ctx,T_DEADLINE));
    REQUIRE(gf4_change_end(&host.ctx,T_FAILURE)==T_FAILURE);
    REQUIRE(host.ctx.accepting && !host.ctx.recovery_rc && host.ctx.reusable);
    return 0;
}
/* UNIT 13's B2 stub receives a temporary adjusted mode: copy the clock
 * while the call is live rather than retaining its stack pointer. */
static u32 pll_clock,pll_calls;
static void observe_pll(struct gf4_host *h,const struct gf4_host_event *e)
{
    (void)h;
    if (e->op==D_LOAD && e->p[4]) {
        pll_clock=((const struct gf4_mode *)e->p[0])->t.khz;
        pll_calls++;
    }
}
static int tv_clock_and_divider(void)
{
    setup(); host.observe=observe_pll; pll_calls=0;
    struct gfinit_mode analog;
    REQUIRE(!gf4_mode_calc(&state.modes[0],&state.head[T_HEAD_B].route,
        &legacy.baseline,&host.ctx.limits[T_HEAD_B],&state.head[T_HEAD_B].front,&analog));
    REQUIRE(pll_calls==1 && pll_clock==GF_MODE_CLOCK_KHZ);
    REQUIRE(!(analog.sr[T_SR_CLOCK]&T_SR_DIV2));
    /* Independent oracle: tvnv17.c:360-363; tvmodesnv17.c:123-200.
     * Both interlaced component norms are TV_ENC_MODE, not CTV. */
    static const struct { u32 norm,clock; } cases[]={
        {T_PAL,T_LD_CLOCK},{T_PAL_M,T_LD_CLOCK},{T_PAL_N,T_LD_CLOCK},
        {T_PAL_NC,T_LD_CLOCK},{T_NTSC_M,T_LD_CLOCK},{T_NTSC_J,T_LD_CLOCK},
        {T_HD480I,T_LD_CLOCK},{T_HD480P,T_SD_CLOCK},
        {T_HD576I,T_LD_CLOCK},{T_HD576P,T_SD_CLOCK},
        {T_HD720P,T_HD_CLOCK},{T_HD1080I,T_HD_CLOCK}
    };
    for (u32 i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        setup(); host.observe=observe_pll; pll_calls=0;
        struct gf4_route route=state.head[T_HEAD_B].route;
        route.type=GF4_TV; route.tv_norm=cases[i].norm;
        struct gfinit_mode output;
        REQUIRE(!gf4_mode_calc(&state.modes[0],&route,&legacy.baseline,
            &host.ctx.limits[T_HEAD_B],&state.head[T_HEAD_B].front,&output));
        REQUIRE(pll_calls==1 && pll_clock==cases[i].clock);
        REQUIRE(state.modes[0].t.khz==GF_MODE_CLOCK_KHZ);
        REQUIRE(!(output.sr[T_SR_CLOCK]&T_SR_DIV2) && !host.calls[D_WRITE]);
    }
    /* tvmodesnv17.c:439-443: 320x200 has DBLSCAN and CLKDIV2.
     * A0's input timing is already normalized; B1/B2 are stubbed here. */
    setup(); host.observe=observe_pll; pll_calls=0;
    struct gf4_route route=state.head[T_HEAD_B].route;
    route.type=GF4_TV; route.tv_norm=T_NTSC_M;
    state.modes[0].t.hd=T_LD_WIDTH;
    state.modes[0].t.vd=T_LD_HEIGHT*T_LD_SCAN_FACTOR;
    state.modes[0].t.flags|=GF4_DBLSCAN;
    struct gfinit_mode output;
    REQUIRE(!gf4_mode_calc(&state.modes[0],&route,&legacy.baseline,
        &host.ctx.limits[T_HEAD_B],&state.head[T_HEAD_B].front,&output));
    REQUIRE(pll_clock==T_LD_CLOCK && pll_calls==1);
    REQUIRE(output.sr[T_SR_CLOCK]==(GF_MODE_SR_BLANKED|T_SR_DIV2));
    REQUIRE(!gf4_change_begin(&host.ctx,T_DEADLINE));
    state.next[T_HEAD_B].mode=state.modes[0];
    REQUIRE(!gf4_mode_load(&host.ctx,T_HEAD_B,&output,T_DEADLINE));
    REQUIRE(host.sr[T_HEAD_B][T_SR_CLOCK]==(T_SR_RUN|T_SR_DIV2));
    state.next[T_HEAD_B].blank=true;
    REQUIRE(!gf4_mode_load(&host.ctx,T_HEAD_B,&output,T_DEADLINE));
    REQUIRE(host.sr[T_HEAD_B][T_SR_CLOCK]==(T_SR_RUN|T_SR_OFF|T_SR_DIV2));
    state.next[T_HEAD_B].blank=false; state.next[T_HEAD_B].dpms=GF4_OFF;
    REQUIRE(!gf4_mode_load(&host.ctx,T_HEAD_B,&output,T_DEADLINE));
    REQUIRE(host.sr[T_HEAD_B][T_SR_CLOCK]==(T_SR_RUN|T_SR_OFF|T_SR_DIV2));
    return 0;
}
static int inactive_heads(void)
{
    /* Fail any unnecessary B7 call: the real B7 validates enabled/DPMS/IRQ
     * before looking at pending (display/flip.c:15-24,86-95). */
    for (u32 active=0;active<=1U;active++) {
        setup();
        for (u8 i=0;i<GF4_HEADS;i++) state.head[i].enabled=i<active;
        host.base.ch.heads=host.base.f.wanted_heads=active ? 1U : 0;
        gf4_host_fail_at(&host,D_FLIP_COMPLETE,0,GF4_ESTATE);
        struct gf4_route route=state.head[T_HEAD_B].route;
        struct gf3_surface front=state.head[T_HEAD_B].front;
        REQUIRE(!gf4_set_mode(&host.ctx,&route,T_NEW_ID,&front,T_DEADLINE));
        REQUIRE(state.head[T_HEAD_B].enabled && host.ctx.accepting);
        REQUIRE(!host.calls[D_FLIP_COMPLETE]);
    }
    setup();
    state.head[T_HEAD_B].dpms=GF4_OFF;
    host.base.ch.heads=host.base.f.wanted_heads=1U;
    gf4_host_fail_at(&host,D_FLIP_COMPLETE,0,GF4_ESTATE);
    REQUIRE(!gf4_change_begin(&host.ctx,T_DEADLINE));
    state.next[T_HEAD_B].dpms=GF4_ON;
    REQUIRE(!gf4_change_end(&host.ctx,0));
    REQUIRE(state.head[T_HEAD_B].dpms==GF4_ON && !host.calls[D_FLIP_COMPLETE]);
    REQUIRE(host.base.ch.heads==((1U<<GF4_HEADS)-1U) && host.ctx.accepting);
    /* A pending hold must still be completed before rollback is captured. */
    setup();
    host.ctx.hold[T_HEAD_B]=(struct gf4_flip_hold){.pending=true,
        .old_front=state.head[T_HEAD_B].front,.new_front=state.head[0].front};
    REQUIRE(!gf4_change_begin(&host.ctx,T_DEADLINE));
    REQUIRE(host.calls[D_FLIP_COMPLETE]==1 && !host.ctx.hold[T_HEAD_B].pending);
    REQUIRE(state.rollback.head[T_HEAD_B].front.off==state.head[0].front.off);
    REQUIRE(!gf4_change_end(&host.ctx,0));
    return 0;
}
static int saved_and_deadline(void)
{
    setup();
    host.ctx.saved_closed=true; host.ctx.diagnostic_saved=&state.rollback;
    host.ctx.accepting=false;
    REQUIRE(!gf4_change_begin(&host.ctx,T_DEADLINE));
    REQUIRE(!gf4_change_end(&host.ctx,0));
    REQUIRE(host.ctx.accepting && events[host.nevents-1].op==D_ADMISSION);

    setup();
    host.ctx.saved_closed=true; host.ctx.diagnostic_saved=0;
    host.ctx.accepting=false;
    REQUIRE(!gf4_change_begin(&host.ctx,T_DEADLINE));
    REQUIRE(!gf4_change_end(&host.ctx,0));
    REQUIRE(!host.ctx.accepting && host.calls[D_ADMISSION]==1);
    setup();
    REQUIRE(!gf4_change_begin(&host.ctx,T_DEADLINE));
    REQUIRE(gf4_mode_load(&host.ctx,T_HEAD_B,&state.work[T_HEAD_B],T_DEADLINE-1U)==GF4_ESTATE);
    host.local_now=T_DEADLINE;
    REQUIRE(gf4_mode_load(&host.ctx,T_HEAD_B,&state.work[T_HEAD_B],T_DEADLINE)==GF4_ETIME);
    REQUIRE(!host.calls[D_WRITE]);
    REQUIRE(gf4_change_end(&host.ctx,T_FAILURE)==T_FAILURE);
    REQUIRE(!host.ctx.accepting && !host.ctx.reusable && host.ctx.recovery_rc==GF4_ETIME);
    return 0;
}
void _start(void)
{
    int rc=load_and_commit();
    if (!rc) rc=failures();
    if (!rc) rc=preparation_and_io();
    if (!rc) rc=saved_and_deadline();
    if (!rc) rc=inactive_heads();
    if (!rc) rc=tv_clock_and_divider();
    /* Every failed REQUIRE exits nonzero even beyond the 8-bit exit range. */
    __asm__ volatile ("int $0x80" : : "a"(1),"b"(rc ? 1 : 0));
    __builtin_unreachable();
}
