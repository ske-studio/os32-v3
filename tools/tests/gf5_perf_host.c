/* Link hwdev/gf/perf.c, UNIT 0, freestanding (-nostdlib).
 * Golden ROM fields are independently transcribed from MIT nouveau c03c1a1
 * nvkm/subdev/bios/perf.c:30-145; the signed NV4a example is 325-25 MHz,
 * memory 200 MHz * 2000 = 400000 kHz. No installed refs are read at runtime.
 * Call oracle: nvkm/subdev/clk/base.c:159-215,266-305 plus GF_FEAT5_DESIGN
 * §5 (stop on error), final shared adapter contract, and PM decisions 15/21/22.
 * Observe only seeds dependency outputs; all stubs belong to gf5_fake.h. */
#define GF5_HOST_UNIT 0
#include "gf5_fake.h"

enum {
    ROM_BYTES=1024, BIT_AT=0x20, BIT_HEAD=12, BIT_STRIDE=6,
    POWER_AT=0x60, PERF_AT=0x180, PERF_HEAD=8, ENTRY_LEN=0x40,
    SUB_COUNT=2, SUB_LEN=3, ENTRY_STRIDE=ENTRY_LEN+SUB_COUNT*SUB_LEN,
    BMP_AT=0x80, BMP_PTR=0x94, BMP_HEADER=4,
    TRACE_CAP=64, OUTER_DL=123456,
    KEEP_COUNT=0xabcdef,
    /* Synthetic ROM inputs; layout/scales: nouveau c03c1a1
     * nvkm/subdev/bios/perf.c:30-145. Two entries exercise signed core
     * offsets and subentry stride; these are fixture choices, not HW limits. */
    ENTRY_COUNT=2, FAN_BASE=60, VOLT_BASE=7,
    SHADER_MHZ=300, SHADER_STEP_MHZ=25, CORE_OFFSET_MHZ=-25,
    MEM_MHZ=200, MEM_STEP_MHZ=10,
    KHZ_PER_MHZ=1000, NV4_MEM_KHZ_PER_UNIT=2000,
    NEW_CORE_MHZ=275, NEW_SHADER_MHZ=300, NEW_MEM_MHZ=350,
    NEW_FAN=55, NEW_VOLT=9,
    OLD_CORE_UNITS=30000, OLD_MEM_UNITS=20000, OLD_FAN=70, OLD_VOLT=11,
    OLD_CORE_KHZ_PER_UNIT=10, OLD_MEM_KHZ_PER_UNIT=20,
    OLD_VOLT_OFFSET=0x38, NV4_MIN_ENTRY=0x0d, BIT_PERF_MIN_HEAD=6
};
static struct gf5_host h;
static struct gf4_state state;
static struct gf5_host_event events[TRACE_CAP];
static struct gf4_host_event display_events[TRACE_CAP];
static u8 rom[ROM_BYTES];
static struct gf5_image image;
/* Distinct stub outputs prove the identical complete plan crosses both
 * program boundaries. These are identity tokens, not PLL golden values. */
static const struct gf5_plan planned={
    .nc=0x11,.nn=0x22,.sc=0x33,.mux=0x44,.mc=0x55,.mn=0x66,
    .actual={SHADER_MHZ*KHZ_PER_MHZ,SHADER_MHZ*KHZ_PER_MHZ,
        MEM_MHZ*NV4_MEM_KHZ_PER_UNIT}
};
static const struct gf5_clock requested={
    SHADER_MHZ*KHZ_PER_MHZ,SHADER_MHZ*KHZ_PER_MHZ,
    MEM_MHZ*NV4_MEM_KHZ_PER_UNIT
};
static bool saw_ram,saw_core,nested_restore;
static i32 lower_source;
static u32 selected_fan,selected_volt;
static bool heat_fixture,heat_ignore_write;
static u32 heat_corrupt_domain;
static struct gf5_clock heat_current,heat_request;
/* Independent synthetic PLL oracle, MIT nouveau clk/nv40.c:39-78:
 * 27 MHz reference, M=1, P=0, bypass stage 2, N={4,5,6,7,8,12}.
 * actual deliberately differs from decoded/readback: PM final P0 decision
 * removed actual equality (including fixedgain2); decoded equality is required. */
static const struct { u32 khz,nn; } heat_pll[]={
    {108000,0x0401},{135000,0x0501},{162000,0x0601},
    {189000,0x0701},{216000,0x0801},{324000,0x0c01}
};
static void heat_observe(const struct gf5_host_event *e)
{
    assert(e->op==F5_CALC || e->op==F5_PROG);
    if (e->op==F5_CALC && e->a[0]==F5_CALL_READ) {
        struct gf5_clock readback=heat_current;
        if (h.calls[F5_PROG]) {
            if (heat_corrupt_domain==1) ++readback.shader_khz;
        }
        *(struct gf5_clock *)e->p[1]=readback;
    } else if (e->op==F5_CALC) {
        const struct gf5_clock *c=e->p[1];
        assert(c->mem_khz==400000);
        u32 i;
        for (i=0;i<sizeof(heat_pll)/sizeof(heat_pll[0]);++i)
            if (heat_pll[i].khz==c->core_khz) break;
        assert(i<sizeof(heat_pll)/sizeof(heat_pll[0]));
        u32 shader;
        for (shader=0;shader<sizeof(heat_pll)/sizeof(heat_pll[0]);++shader)
            if (heat_pll[shader].khz==c->shader_khz) break;
        assert(shader<sizeof(heat_pll)/sizeof(heat_pll[0]));
        *(struct gf5_plan *)e->p[2]=(struct gf5_plan){
            .nc=0x80000100,.nn=heat_pll[i].nn,
            .sc=0x80000000|heat_pll[shader].nn,.mux=i==shader ? 0x33 : 0x23,
            .actual={c->core_khz+1,c->shader_khz+1,c->mem_khz}};
        heat_request=*c;
    } else {
        const struct gf5_plan *p=e->p[1];
        assert(p->nc==0x80000100 && (p->mux==0x33 || p->mux==0x23));
        if (!heat_ignore_write) heat_current=heat_request;
    }
}

static void put16(u32 at,u32 value)
{
    assert(at+2<=sizeof(rom)); rom[at]=(u8)value; rom[at+1]=(u8)(value>>8);
}
static void put32(u32 at,u32 value)
{
    put16(at,value); put16(at+2,value>>16);
}
static bool same_clock(struct gf5_clock a,struct gf5_clock b)
{
    return a.core_khz==b.core_khz && a.shader_khz==b.shader_khz && a.mem_khz==b.mem_khz;
}
static bool same_entry(struct gf5_perf_entry a,struct gf5_perf_entry b)
{
    return same_clock(a.clock,b.clock) && a.voltage==b.voltage && a.fanspeed==b.fanspeed &&
        a.pstate==b.pstate && a.bios_index==b.bios_index;
}
static void observe(struct gf5_host *host,const struct gf5_host_event *e)
{
    assert(host==&h && h.ctx.deadline==OUTER_DL && h.ctx.image==&image);
    /* UNIT0 RAM is a boundary stub: B2 owns its internal pause/resume.
     * B0 must leave the enclosing service's publication state untouched. */
    assert(!h.calls[F5_PAUSE] && !h.calls[F5_RESUME] && !h.calls[F5_ADMIT]);
    assert(h.ctx.accepting==!nested_restore &&
        h.display.ctx.accepting==!nested_restore &&
        h.display.scan_hook_open==!nested_restore &&
        h.display.ctx.saved_closed==nested_restore);
    if (e->rc) {
        if (lower_source) h.ctx.source_rc=lower_source;
        return;
    }
    if (heat_fixture) { heat_observe(e); return; }
    if (e->op==F5_CALC) {
        assert(e->a[0]==F5_CALL_CALC);
        const struct gf5_clock *request=e->p[1];
        assert(same_clock(*request,requested));
        *(struct gf5_plan *)e->p[2]=planned;
    }
    if (e->op==F5_RAM || e->op==F5_PROG) {
        const struct gf5_plan *p=e->p[1];
        assert(p->nc==planned.nc && p->nn==planned.nn && p->sc==planned.sc &&
            p->mux==planned.mux && p->mc==planned.mc && p->mn==planned.mn &&
            same_clock(p->actual,planned.actual));
        if (e->op==F5_RAM) {
            saw_ram=true;
        } else {
            assert(saw_ram && !h.display.base.paused); saw_core=true;
        }
    }
    if (e->op==F5_FAN) {
        assert(e->a[0]==F5_CALL_CSTATE && e->a[1]==selected_fan);
        /* The fixture intentionally leaves policy updates to observation. */
        if ((i32)e->a[2]>0) h.ctx.fan.cstate=selected_fan;
    }
}
static void fresh(void)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf5_host_init(&h,&state);
    /* Model the outer service's published RUN state (not set by F4 init). */
    h.display.scan_hook_open=true;
    h.events=events; h.cevents=TRACE_CAP; h.observe=observe;
    h.display.events=display_events; h.display.cevents=TRACE_CAP;
    h.ctx.deadline=OUTER_DL; h.ctx.image=&image;
    h.ctx.rom=(struct gf5_rom_view){.bytes=rom,.size=sizeof(rom)};
    gf2_host_memset(rom,0,sizeof(rom));
    /* Shared BIT directory format, hwdev/gf/rom.inc; one BIT P v2 entry. */
    static const u8 bit[]={0xff,0xb8,'B','I','T',0,0};
    gf2_host_memcpy(rom+BIT_AT,bit,sizeof(bit));
    rom[BIT_AT+8]=BIT_HEAD; rom[BIT_AT+9]=BIT_STRIDE; rom[BIT_AT+10]=1;
    rom[BIT_AT+BIT_HEAD]='P'; rom[BIT_AT+BIT_HEAD+1]=2;
    put16(BIT_AT+BIT_HEAD+2,4); put16(BIT_AT+BIT_HEAD+4,POWER_AT);
    put32(POWER_AT,PERF_AT);
    rom[PERF_AT]=0x21; rom[PERF_AT+1]=PERF_HEAD; rom[PERF_AT+2]=ENTRY_COUNT;
    rom[PERF_AT+3]=ENTRY_LEN; rom[PERF_AT+4]=SUB_COUNT; rom[PERF_AT+5]=SUB_LEN;
    for (u32 i=0;i<ENTRY_COUNT;++i) {
        u32 at=PERF_AT+PERF_HEAD+i*ENTRY_STRIDE;
        rom[at+4]=(u8)(FAN_BASE+i); rom[at+5]=(u8)(VOLT_BASE+i);
        put16(at+6,SHADER_MHZ+i*SHADER_STEP_MHZ); rom[at+8]=i ? (u8)CORE_OFFSET_MHZ : 0;
        put16(at+0x0b,MEM_MHZ+i*MEM_STEP_MHZ);
    }
    saw_ram=saw_core=nested_restore=heat_fixture=false; lower_source=0;
    selected_fan=FAN_BASE; selected_volt=VOLT_BASE;
}
static void expect_get(u32 index,struct gf5_perf_entry expected)
{
    struct gf5_perf_entry e={0};
    assert(!gf5_perf_get(&h.ctx,index,&e) && same_entry(e,expected));
    assert(!h.nevents && !h.display.nevents);
}
static void versions(void)
{
    static const u8 nv4_versions[]={0x21,0x23,0x24};
    for (u32 i=0;i<sizeof(nv4_versions);++i) {
        fresh(); rom[PERF_AT]=nv4_versions[i];
        u32 n=0; assert(!gf5_perf_count(&h.ctx,&n) && n==ENTRY_COUNT);
        expect_get(0,(struct gf5_perf_entry){requested,VOLT_BASE,FAN_BASE,0,0});
        expect_get(1,(struct gf5_perf_entry){{(SHADER_MHZ+SHADER_STEP_MHZ+CORE_OFFSET_MHZ)*KHZ_PER_MHZ,
            (SHADER_MHZ+SHADER_STEP_MHZ)*KHZ_PER_MHZ,
            (MEM_MHZ+MEM_STEP_MHZ)*NV4_MEM_KHZ_PER_UNIT},VOLT_BASE+1,FAN_BASE+1,0,1});
    }
    static const u8 newer[]={0x25,0x30,0x35};
    for (u32 i=0;i<sizeof(newer);++i) {
        fresh(); rom[PERF_AT]=newer[i];
        u32 at=PERF_AT+PERF_HEAD;
        gf2_host_memset(rom+at,0,ENTRY_LEN);
        u32 fan=i ? 6 : 4, core=i ? 8 : 6;
        rom[at+fan]=NEW_FAN; rom[at+fan+1]=NEW_VOLT;
        put16(at+core,NEW_CORE_MHZ); put16(at+0x0a,NEW_SHADER_MHZ); put16(at+0x0c,NEW_MEM_MHZ);
        expect_get(0,(struct gf5_perf_entry){{NEW_CORE_MHZ*KHZ_PER_MHZ,NEW_SHADER_MHZ*KHZ_PER_MHZ,
            NEW_MEM_MHZ*KHZ_PER_MHZ},NEW_VOLT,NEW_FAN,0,0});
    }
    static const u8 old[]={0x12,0x13,0x14};
    for (u32 i=0;i<sizeof(old);++i) {
        fresh(); put32(POWER_AT,0); /* BIT P absent perf -> BMP fallback. */
        static const u8 bmp[]={0xff,0x7f,'N','V',0};
        gf2_host_memcpy(rom+BMP_AT,bmp,sizeof(bmp));
        rom[BMP_AT+6]=0x25; put16(BMP_AT+BMP_PTR,PERF_AT);
        rom[PERF_AT]=BMP_HEADER; rom[PERF_AT+1]=old[i];
        rom[PERF_AT+2]=1; rom[PERF_AT+3]=ENTRY_LEN;
        u32 at=PERF_AT+BMP_HEADER;
        gf2_host_memset(rom+at,0,ENTRY_LEN);
        put32(at+1,OLD_CORE_UNITS); put32(at+5,OLD_MEM_UNITS);
        rom[at+0x37]=OLD_FAN; rom[at+OLD_VOLT_OFFSET]=OLD_VOLT;
        expect_get(0,(struct gf5_perf_entry){{OLD_CORE_UNITS*OLD_CORE_KHZ_PER_UNIT,0,
            OLD_MEM_UNITS*OLD_MEM_KHZ_PER_UNIT},OLD_VOLT,OLD_FAN,0,0});
        rom[PERF_AT+3]=OLD_VOLT_OFFSET; /* Voltage is optional in these versions. */
        expect_get(0,(struct gf5_perf_entry){{OLD_CORE_UNITS*OLD_CORE_KHZ_PER_UNIT,0,
            OLD_MEM_UNITS*OLD_MEM_KHZ_PER_UNIT},0,OLD_FAN,0,0});
        put32(at+1,0xffffffffU);
        struct gf5_perf_entry e={0};
        assert(gf5_perf_get(&h.ctx,0,&e)==GF5_EBIOS && !e.clock.core_khz);
    }
}
static void malformed(void)
{
    u32 n;
    struct gf5_perf_entry keep={{1,2,3},4,5,0,0},e;
    fresh(); e=keep;
    assert(gf5_perf_get(&h.ctx,ENTRY_COUNT,&e)==GF5_EINVAL && same_entry(e,keep));
    assert(gf5_perf_count(0,&n)==GF5_EINVAL);
    assert(gf5_perf_count(&h.ctx,0)==GF5_EINVAL);
    assert(gf5_perf_get(&h.ctx,0,0)==GF5_EINVAL);
    fresh(); h.ctx.rom.bytes=0; n=KEEP_COUNT;
    assert(gf5_perf_count(&h.ctx,&n)==GF5_ENOTSUP && n==KEEP_COUNT);
    for (u32 size=0;size<PERF_HEAD+ENTRY_COUNT*ENTRY_STRIDE;++size) {
        fresh(); h.ctx.rom.size=PERF_AT+size; n=KEEP_COUNT;
        assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS && n==KEEP_COUNT);
        e=keep; assert(gf5_perf_get(&h.ctx,0,&e)==GF5_EBIOS && same_entry(e,keep));
    }
    fresh(); h.ctx.rom.size=PERF_AT+PERF_HEAD+ENTRY_COUNT*ENTRY_STRIDE;
    assert(!gf5_perf_count(&h.ctx,&n) && n==ENTRY_COUNT); /* Exact full-span control. */
    fresh(); put32(POWER_AT,0xffffffffU);
    assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS);
    fresh(); put16(BIT_AT+BIT_HEAD+2,3);
    assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS);
    fresh(); rom[BIT_AT+9]=BIT_STRIDE-1;
    assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS);
    fresh(); rom[PERF_AT+1]=BIT_PERF_MIN_HEAD-1;
    assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS);
    fresh(); rom[PERF_AT+3]=NV4_MIN_ENTRY-1;
    assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS);
    fresh(); rom[PERF_AT]=0x40;
    assert(gf5_perf_count(&h.ctx,&n)==GF5_ENOTSUP);
    fresh(); put16(PERF_AT+PERF_HEAD+6,0); rom[PERF_AT+PERF_HEAD+8]=(u8)-1;
    e=keep; assert(gf5_perf_get(&h.ctx,0,&e)==GF5_EBIOS && same_entry(e,keep));
    fresh(); rom[PERF_AT+2]=0; assert(!gf5_perf_count(&h.ctx,&n) && !n);
    fresh(); gf2_host_memset(rom,0,sizeof(rom)); n=KEEP_COUNT;
    assert(gf5_perf_count(&h.ctx,&n)==GF5_ENOTSUP && n==KEEP_COUNT);
    assert(!h.nevents && !h.display.nevents);
}

struct call { enum gf5_host_op op; i32 direction; };
static const struct call golden[]={
    {F5_CALC,0},{F5_FAN_CHECK,0},{F5_VOLT_CHECK,0},{F5_RAM,0},
    {F5_FAN,1},{F5_VOLT,1},{F5_PROG,0},{F5_VOLT,-1},{F5_FAN,-1}
};
static void prefix(u32 count)
{
    assert(h.nevents>=count);
    for (u32 i=0;i<count;++i) {
        const struct gf5_host_event *e=&events[i];
        assert(e->op==golden[i].op && e->p[0]==&h.ctx);
        if (e->op==F5_FAN_CHECK) assert(e->a[0]==selected_fan);
        if (e->op==F5_VOLT_CHECK) assert(e->a[0]==selected_volt && e->a[1]==selected_volt);
        if (e->op==F5_VOLT) {
            assert(e->a[0]==F5_CALL_ID && e->a[1]==selected_volt && e->a[2]==selected_volt &&
                (i32)e->a[3]==golden[i].direction);
        }
        if (e->op==F5_FAN) assert((i32)e->a[2]==golden[i].direction);
    }
}
static void selection(void)
{
    const u32 count=sizeof(golden)/sizeof(golden[0]);
    fresh();
    assert(!gf5_perf_select(&h.ctx,0)); prefix(count);
    assert(h.nevents==count && saw_ram && saw_core && h.ctx.accepting &&
        h.ctx.phase==GF5_RUN && !h.display.base.paused);
    assert(!h.ctx.last_rc && !h.ctx.source_rc && h.ctx.fan.cstate==FAN_BASE);
    /* Both RUN and nested RESTORING retain their caller's publication state.
     * Removing the links proves B0 does not own or require RAM's adapters. */
    for (u32 closed=0;closed<2;++closed) {
        fresh(); nested_restore=closed!=0;
        h.ctx.busy=nested_restore;
        h.ctx.phase=nested_restore ? GF5_RESTORING : GF5_RUN;
        h.ctx.accepting=h.display.ctx.accepting=h.display.scan_hook_open=!nested_restore;
        h.display.ctx.saved_closed=nested_restore;
        h.ctx.links.drain_pause=0; h.ctx.links.unpause=0; h.ctx.links.admission=0;
        assert(!gf5_perf_select(&h.ctx,0)); prefix(count);
        assert(h.nevents==count && saw_ram && saw_core &&
            h.ctx.phase==(nested_restore ? GF5_RESTORING : GF5_RUN) &&
            h.ctx.accepting==!nested_restore && h.display.ctx.saved_closed==nested_restore &&
            h.ctx.busy==nested_restore && !h.display.nevents);
    }
    /* Each failed dependency is the last call, including during restore.
     * B0 marks FAULT/closed locally; no later lowering or adapter is allowed. */
    for (u32 closed=0;closed<2;++closed) for (u32 i=0;i<count;++i) {
        fresh(); nested_restore=closed!=0;
        h.ctx.busy=nested_restore;
        enum gf5_phase phase=nested_restore ? GF5_RESTORING : GF5_RUN;
        h.ctx.phase=phase;
        h.ctx.accepting=h.display.ctx.accepting=h.display.scan_hook_open=!nested_restore;
        h.display.ctx.saved_closed=nested_restore;
        u32 nth=1;
        for (u32 j=0;j<i;++j) if (golden[j].op==golden[i].op) ++nth;
        gf5_host_fail_at(&h,golden[i].op,nth,GF5_EIO);
        assert(gf5_perf_select(&h.ctx,0)==GF5_EIO); prefix(i+1);
        assert(h.nevents==i+1 && h.ctx.last_rc==GF5_EIO && h.ctx.source_rc==GF5_EIO);
        assert(h.ctx.phase==(i<3 ? phase : GF5_FAULT) &&
            h.ctx.accepting==(i<3 && !nested_restore));
        assert(!h.calls[F5_PAUSE] && !h.calls[F5_RESUME] && !h.calls[F5_ADMIT] &&
            !h.display.base.paused && h.display.ctx.saved_closed==nested_restore);
        assert(h.ctx.image==&image && h.ctx.rom.bytes==rom &&
            h.ctx.rom.size==sizeof(rom) && !h.live_allocs && h.ctx.deadline==OUTER_DL &&
            h.ctx.busy==nested_restore && !h.display.nevents);
    }
    /* EBIOS from calc (including Ra266 separate shader) never pauses. */
    fresh(); gf5_host_fail_at(&h,F5_CALC,1,GF5_EBIOS);
    assert(gf5_perf_select(&h.ctx,0)==GF5_EBIOS && h.nevents==1 && h.ctx.phase==GF5_RUN);
    /* PM22: classify at the public boundary while retaining the lower rc. */
    fresh(); gf5_host_fail_at(&h,F5_RAM,1,GF2_ETIME);
    assert(gf5_perf_select(&h.ctx,0)==GF5_ETIME && h.ctx.source_rc==GF2_ETIME);
    assert(h.calls[F5_CALC]==1 && h.calls[F5_RAM]==1 &&
        h.nevents==h.calls[F5_CALC]+h.calls[F5_RAM]+
            h.calls[F5_FAN_CHECK]+h.calls[F5_VOLT_CHECK] && !h.calls[F5_ADMIT] &&
        h.ctx.last_rc==GF5_ETIME && h.ctx.phase==GF5_FAULT && !h.ctx.accepting);
    fresh(); lower_source=GF2_ETIME; gf5_host_fail_at(&h,F5_CALC,1,GF5_ETIME);
    assert(gf5_perf_select(&h.ctx,0)==GF5_ETIME && h.ctx.source_rc==GF2_ETIME &&
        h.ctx.last_rc==GF5_ETIME && h.nevents==1 && h.ctx.phase==GF5_RUN && h.ctx.accepting);
    fresh(); lower_source=GF2_EIO; gf5_host_fail_at(&h,F5_PROG,1,GF5_EIO);
    assert(gf5_perf_select(&h.ctx,0)==GF5_EIO && h.ctx.source_rc==GF2_EIO);
    /* nouveau clk/base.c:178-213: only confirmed absence is optional. */
    fresh(); gf5_host_fail_at(&h,F5_FAN,0,GF5_ENODEV);
    gf5_host_fail_at(&h,F5_VOLT,0,GF5_ENODEV);
    gf5_host_fail_at(&h,F5_FAN_CHECK,0,GF5_ENODEV);
    gf5_host_fail_at(&h,F5_VOLT_CHECK,0,GF5_ENODEV);
    assert(!gf5_perf_select(&h.ctx,0)); prefix(count);
    assert(h.nevents==count && h.ctx.last_rc==GF5_ENODEV &&
        h.ctx.source_rc==GF5_ENODEV && h.ctx.phase==GF5_RUN);
    static const enum gf5_host_op optional[]={F5_FAN_CHECK,F5_VOLT_CHECK,F5_FAN,F5_VOLT};
    for (u32 i=0;i<sizeof(optional)/sizeof(optional[0]);++i) {
        fresh(); gf5_host_fail_at(&h,optional[i],1,GF5_ENOTSUP);
        assert(gf5_perf_select(&h.ctx,0)==GF5_ENOTSUP);
        assert(h.ctx.last_rc==GF5_ENOTSUP && h.ctx.source_rc==GF5_ENOTSUP);
        assert(h.ctx.phase==(i<2 ? GF5_RUN : GF5_FAULT));
        assert(h.ctx.accepting==(i<2) && !h.calls[F5_PROG]);
        if (i<2) assert(!h.calls[F5_RAM] && !h.calls[F5_FAN] && !h.calls[F5_VOLT]);
    }
    fresh(); gf5_host_fail_at(&h,F5_RAM,1,GF5_ENOTSUP);
    assert(gf5_perf_select(&h.ctx,0)==GF5_ENOTSUP && !h.calls[F5_PROG] && h.ctx.phase==GF5_FAULT);
    fresh(); assert(gf5_perf_select(&h.ctx,ENTRY_COUNT)==GF5_EINVAL && !h.nevents);
    assert(gf5_perf_select(0,0)==GF5_EINVAL);
}
/* A13: all supported versions, disabled first/middle/last and all-ff.
 * Raw pstate comes from bios/perf.c:98-101, exclusion clk/base.c:421-425.
 * Disabled entries carry otherwise valid bytes so only pstate excludes them. */
static void dense_entries(void)
{
    static const u8 versions[]={0x12,0x13,0x14,0x21,0x23,0x24,0x25,0x30,0x35};
    enum { RAW_COUNT=5, DISABLED=0xff, VALID_PSTATE=0x10 };
    for (u32 v=0;v<sizeof(versions);++v) for (u32 mask=0;mask<(1U<<RAW_COUNT);++mask) {
        fresh();
        bool old=versions[v]<0x20;
        u32 header=old ? BMP_HEADER : PERF_HEAD;
        u32 stride=old ? ENTRY_LEN : ENTRY_STRIDE;
        if (old) {
            put32(POWER_AT,0);
            static const u8 bmp[]={0xff,0x7f,'N','V',0};
            gf2_host_memcpy(rom+BMP_AT,bmp,sizeof(bmp));
            rom[BMP_AT+6]=0x25; put16(BMP_AT+BMP_PTR,PERF_AT);
            rom[PERF_AT]=BMP_HEADER; rom[PERF_AT+1]=versions[v];
        } else rom[PERF_AT]=versions[v];
        rom[PERF_AT+2]=RAW_COUNT; rom[PERF_AT+3]=ENTRY_LEN;
        for (u32 i=0;i<RAW_COUNT;++i) {
            u32 at=PERF_AT+header+i*stride;
            gf2_host_memset(rom+at,0,stride);
            rom[at]=(mask&(1U<<i)) ? DISABLED : VALID_PSTATE+i;
            if (old) {
                put32(at+1,OLD_CORE_UNITS); put32(at+5,OLD_MEM_UNITS);
                rom[at+0x37]=FAN_BASE+i; rom[at+OLD_VOLT_OFFSET]=VOLT_BASE+i;
            } else if (versions[v]<=0x24) {
                rom[at+4]=FAN_BASE+i; rom[at+5]=VOLT_BASE+i;
                put16(at+6,SHADER_MHZ); put16(at+0x0b,MEM_MHZ);
            } else {
                u32 fan=versions[v]==0x25 ? 4 : 6,core=versions[v]==0x25 ? 6 : 8;
                rom[at+fan]=FAN_BASE+i; rom[at+fan+1]=VOLT_BASE+i;
                put16(at+core,SHADER_MHZ); put16(at+0x0a,SHADER_MHZ);
                put16(at+0x0c,MEM_MHZ*2);
            }
        }
        u32 n=0,wanted=0;
        for (u32 i=0;i<RAW_COUNT;++i) if (!(mask&(1U<<i))) ++wanted;
        assert(!gf5_perf_count(&h.ctx,&n) && n==wanted);
        u32 dense=0;
        for (u32 raw=0;raw<RAW_COUNT;++raw) if (!(mask&(1U<<raw))) {
            struct gf5_perf_entry e={.clock=requested,.voltage=VOLT_BASE+raw,.fanspeed=FAN_BASE+raw,
                .pstate=VALID_PSTATE+raw,.bios_index=raw};
            if (old) { e.clock.core_khz=OLD_CORE_UNITS*OLD_CORE_KHZ_PER_UNIT;
                e.clock.shader_khz=0; e.clock.mem_khz=OLD_MEM_UNITS*OLD_MEM_KHZ_PER_UNIT; }
            expect_get(dense++,e);
        }
        struct gf5_perf_entry keep={{1,2,3},4,5,6,7},e=keep;
        assert(gf5_perf_get(&h.ctx,n,&e)==GF5_EINVAL && same_entry(e,keep));
        assert(gf5_perf_select(&h.ctx,n)==GF5_EINVAL && !h.nevents);
        /* Distinct fan/voltage tokens prove select uses the same dense
         * index as get, including when raw zero is disabled. */
        u32 raw=0;
        if (!old) for (u32 i=0;i<n;++i) {
            while (mask&(1U<<raw)) ++raw;
            selected_fan=FAN_BASE+raw; selected_volt=VOLT_BASE+raw; ++raw;
            gf5_host_trace_clear(&h); saw_ram=saw_core=false;
            assert(!gf5_perf_select(&h.ctx,i));
            prefix(sizeof(golden)/sizeof(golden[0]));
        }
    }
}

/* U3 calls once per valid sample. therm's sensor deadline and edge-to-level
 * dispatch are U4's responsibility; no catch-up loop is simulated here. */
enum { HEAT_DOWN=80, HEAT_HYST=5, HEAT_SHUTDOWN=110,
       HEAT_HOT=80000, HEAT_COOL=74999, HEAT_EQUAL=75000 };
static void heat_fresh(void)
{
    fresh(); heat_fixture=true;
    h.ctx.fan.threshold[1]=HEAT_DOWN; h.ctx.fan.hyst[1]=HEAT_HYST;
    h.ctx.fan.threshold[3]=HEAT_SHUTDOWN;
    h.ctx.ch->f->running=true;
    heat_current=(struct gf5_clock){216000,216000,400000};
    /* Unsorted BIOS list includes a faster-than-entry state. No fan or
     * voltage operation is allowed; BIOS memory values must be ignored. */
    static const u16 mhz[]={324,135,189,108,162};
    rom[PERF_AT]=0x25; rom[PERF_AT+2]=sizeof(mhz)/sizeof(mhz[0]);
    for (u32 i=0;i<sizeof(mhz)/sizeof(mhz[0]);++i) {
        u32 at=PERF_AT+PERF_HEAD+i*ENTRY_STRIDE;
        gf2_host_memset(rom+at,0,ENTRY_STRIDE); rom[at]=i+1;
        put16(at+6,mhz[i]); put16(at+0x0a,mhz[i]); put16(at+0x0c,777);
    }
}
static i32 heat_sample(i32 milli_c,bool calibrated)
{
    gf5_host_trace_clear(&h);
    gf5_host_time_set(&h,h.now+1000000U);
    struct gf5_thermal t={.milli_c=milli_c,.calibrated=calibrated};
    i32 rc=gf5_heat_step(&h.ctx,&t,h.now);
    assert(!h.calls[F5_RAM] && !h.calls[F5_FAN] && !h.calls[F5_VOLT] &&
        !h.calls[F5_PAUSE] && !h.calls[F5_RESUME] && !h.display.nevents);
    return rc;
}
static void heat_at(u32 khz,u32 writes)
{
    assert(heat_current.core_khz==khz && heat_current.shader_khz==khz &&
        heat_current.mem_khz==400000 && h.calls[F5_PROG]==writes);
    assert(h.ctx.heat.ceiling.core_khz==khz && h.ctx.heat.ceiling.shader_khz==khz &&
        h.ctx.heat.ceiling.mem_khz==400000 && !h.ctx.stop->state);
}
static void heat_steps(void)
{
    heat_fresh();
    assert(!heat_sample(HEAT_COOL,true) && !h.nevents && !h.ctx.heat.down_active);
    assert(!heat_sample(HEAT_HOT,true)); heat_at(189000,1);
    assert(same_clock(h.ctx.heat.entry_clock,(struct gf5_clock){216000,216000,400000}));
    assert(!heat_sample(HEAT_HOT,true)); heat_at(162000,1);
    assert(!heat_sample(HEAT_HOT,true)); heat_at(135000,1);
    assert(!heat_sample(HEAT_HOT,true)); heat_at(108000,1);
    assert(!heat_sample(HEAT_HOT,true)); heat_at(108000,0);
    /* Two cool samples do nothing; above the boundary resets them. */
    for (u32 i=1;i<=2;++i) {
        assert(!heat_sample(HEAT_COOL,true)); heat_at(108000,0);
        assert(h.ctx.heat.cool_samples==i);
    }
    assert(!heat_sample(HEAT_EQUAL+1,true)); heat_at(108000,0);
    assert(!h.ctx.heat.cool_samples);
    for (u32 step=0;step<4;++step) {
        u32 before=heat_current.core_khz;
        for (u32 i=1;i<=3;++i) {
            assert(!heat_sample(HEAT_EQUAL,true));
            heat_at(i==3 ? before+27000 : before,i==3);
            assert(h.ctx.heat.cool_samples==(i==3 ? 0 : i));
        }
    }
    assert(!h.ctx.heat.down_active); /* 216 MHz, never BIOS 324 MHz. */
    assert(!heat_sample(HEAT_COOL,true) && !h.nevents);
    assert(!heat_sample(HEAT_HOT,true)); heat_at(189000,1);
    /* Reheat after one recovery step immediately lowers again. */
    assert(!heat_sample(HEAT_HOT,true)); heat_at(162000,1);
    for (u32 i=0;i<3;++i) assert(!heat_sample(HEAT_COOL,true));
    heat_at(189000,1);
    assert(!heat_sample(HEAT_HOT,true)); heat_at(162000,1);
    /* Critical owns pause; no recovery while it is held. */
    h.ctx.heat.critical_paused=true;
    for (u32 i=0;i<4;++i) assert(!heat_sample(HEAT_COOL,true) && !h.calls[F5_PROG]);
    assert(!h.ctx.heat.cool_samples);
    h.ctx.heat.critical_paused=false;
    for (u32 i=0;i<2;++i) assert(!heat_sample(HEAT_COOL,true) && !h.calls[F5_PROG]);
    assert(!heat_sample(HEAT_COOL,true)); heat_at(189000,1);
    /* Invalid samples reset cooling and request protective stop. */
    assert(!heat_sample(HEAT_COOL,true) && h.ctx.heat.cool_samples==1);
    assert(heat_sample(HEAT_COOL,false)==GF5_EIO);
    assert(!h.ctx.heat.cool_samples && h.ctx.stop->state && !h.calls[F5_PROG]);
    heat_fresh();
    assert(!heat_sample(HEAT_SHUTDOWN*1000,true) && !h.nevents && h.ctx.stop->state);
    heat_fresh();
    /* Disabled highest lower stage is skipped; no fan is needed. */
    rom[PERF_AT+PERF_HEAD+2*ENTRY_STRIDE]=0xff;
    h.ctx.fan_off=true;
    assert(!heat_sample(HEAT_HOT,true)); heat_at(162000,1);
    /* A BIOS with no lower state holds the entry clock without rewriting. */
    heat_fresh(); rom[PERF_AT+2]=1;
    assert(!heat_sample(HEAT_HOT,true)); heat_at(216000,0);
}
static void heat_shader_order(void)
{
    heat_fresh();
    /* Highest core first, then highest shader. A candidate that raises
     * either domain is unreachable even if its other domain is lower. */
    static const u16 core[]={324,189,189,189,162};
    static const u16 shader[]={324,162,216,216,189};
    for (u32 i=0;i<sizeof(core)/sizeof(core[0]);++i) {
        u32 at=PERF_AT+PERF_HEAD+i*ENTRY_STRIDE;
        put16(at+6,core[i]); put16(at+0x0a,shader[i]);
    }
    assert(!heat_sample(HEAT_HOT,true) && h.calls[F5_PROG]==1);
    assert(same_clock(heat_current,(struct gf5_clock){189000,216000,400000}));
    assert(!heat_sample(HEAT_HOT,true) && h.calls[F5_PROG]==1);
    assert(same_clock(heat_current,(struct gf5_clock){189000,162000,400000}));
    assert(!heat_sample(HEAT_HOT,true) && !h.calls[F5_PROG]);
    assert(same_clock(heat_current,(struct gf5_clock){189000,162000,400000}));
}
static void heat_failures(void)
{
    heat_fresh(); heat_ignore_write=true;
    assert(heat_sample(HEAT_HOT,true)==GF5_EIO);
    assert(h.ctx.stop->state==GF5_STOP_REQUESTED && h.ctx.stop->reason==GF5_EIO);
    assert(h.ctx.heat.ceiling.core_khz==216000); /* Failed prog cannot lower the recorded cap. */
    heat_ignore_write=false;
    for (u32 domain=1;domain<=1;++domain) {
        heat_fresh(); heat_corrupt_domain=domain;
        assert(heat_sample(HEAT_HOT,true)==GF5_EIO && h.ctx.stop->state==GF5_STOP_REQUESTED);
    }
    heat_corrupt_domain=0;
    static const struct { enum gf5_host_op op; u32 nth; } fail[]={
        {F5_CALC,1}, /* current read */
        {F5_CALC,2}, /* candidate calculation */
        {F5_PROG,1}, /* core program */
    };
    for (u32 i=0;i<sizeof(fail)/sizeof(fail[0]);++i) {
        heat_fresh(); gf5_host_fail_at(&h,fail[i].op,fail[i].nth,GF5_EIO);
        assert(heat_sample(HEAT_HOT,true)==GF5_EIO);
        assert(h.ctx.stop->state && h.ctx.stop->reason==GF5_EIO &&
            h.ctx.last_rc==GF5_EIO && h.ctx.source_rc==GF5_EIO);
        assert(!h.calls[F5_PROG] || fail[i].op==F5_PROG);
    }
    heat_fresh(); h.ctx.phase=GF5_FAULT;
    assert(!heat_sample(HEAT_HOT,true) && !h.nevents && !h.ctx.stop->state);
    heat_fresh(); assert(!heat_sample(HEAT_HOT,true));
    ++h.ctx.ch->epoch;
    assert(heat_sample(HEAT_HOT,true)==GF5_ESTATE && !h.nevents && h.ctx.stop->state);
    /* Direct perf requests over the active ceiling are side-effect free. */
    heat_fresh(); assert(!heat_sample(HEAT_HOT,true)); gf5_host_trace_clear(&h);
    assert(gf5_perf_select(&h.ctx,0)==GF5_EBUSY && !h.nevents);
}
void _start(void)
{
    versions(); malformed(); selection(); dense_entries(); heat_steps(); heat_shader_order(); heat_failures();
    __asm__ volatile("int $0x80" : : "a"(1),"b"(0));
    __builtin_unreachable();
}
