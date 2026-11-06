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
    return same_clock(a.clock,b.clock) && a.voltage==b.voltage && a.fanspeed==b.fanspeed;
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
        assert(e->a[0]==F5_CALL_CSTATE && e->a[1]==FAN_BASE);
        /* The fixture intentionally leaves policy updates to observation. */
        if ((i32)e->a[2]>0) h.ctx.fan.cstate=FAN_BASE;
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
    saw_ram=saw_core=nested_restore=false; lower_source=0;
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
            (MEM_MHZ+MEM_STEP_MHZ)*NV4_MEM_KHZ_PER_UNIT},VOLT_BASE+1,FAN_BASE+1,0,0});
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
    {F5_CALC,0},{F5_RAM,0},
    {F5_FAN,1},{F5_VOLT,1},{F5_PROG,0},{F5_VOLT,-1},{F5_FAN,-1}
};
static void prefix(u32 count)
{
    assert(h.nevents>=count);
    for (u32 i=0;i<count;++i) {
        const struct gf5_host_event *e=&events[i];
        assert(e->op==golden[i].op && e->p[0]==&h.ctx);
        if (e->op==F5_VOLT) {
            assert(e->a[0]==F5_CALL_ID && e->a[1]==VOLT_BASE && e->a[2]==VOLT_BASE &&
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
        assert(h.ctx.phase==(!i ? phase : GF5_FAULT) &&
            h.ctx.accepting==(!i && !nested_restore));
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
        h.nevents==h.calls[F5_CALC]+h.calls[F5_RAM] && !h.calls[F5_ADMIT] &&
        h.ctx.last_rc==GF5_ETIME && h.ctx.phase==GF5_FAULT && !h.ctx.accepting);
    fresh(); lower_source=GF2_ETIME; gf5_host_fail_at(&h,F5_CALC,1,GF5_ETIME);
    assert(gf5_perf_select(&h.ctx,0)==GF5_ETIME && h.ctx.source_rc==GF2_ETIME &&
        h.ctx.last_rc==GF5_ETIME && h.nevents==1 && h.ctx.phase==GF5_RUN && h.ctx.accepting);
    fresh(); lower_source=GF2_EIO; gf5_host_fail_at(&h,F5_PROG,1,GF5_EIO);
    assert(gf5_perf_select(&h.ctx,0)==GF5_EIO && h.ctx.source_rc==GF2_EIO);
    /* ENOTSUP is optional only at fan/voltage boundaries, with visible rc. */
    fresh(); gf5_host_fail_at(&h,F5_FAN,0,GF5_ENOTSUP);
    gf5_host_fail_at(&h,F5_VOLT,0,GF5_ENOTSUP);
    assert(!gf5_perf_select(&h.ctx,0)); prefix(count);
    assert(h.nevents==count && h.ctx.last_rc==GF5_ENOTSUP &&
        h.ctx.source_rc==GF5_ENOTSUP && h.ctx.phase==GF5_RUN);
    fresh(); gf5_host_fail_at(&h,F5_FAN,0,GF4_ENODEV);
    assert(!gf5_perf_select(&h.ctx,0)); prefix(count);
    assert(h.nevents==count && h.ctx.last_rc==GF5_ENOTSUP &&
        h.ctx.source_rc==GF4_ENODEV && h.ctx.phase==GF5_RUN);
    fresh(); gf5_host_fail_at(&h,F5_RAM,1,GF5_ENOTSUP);
    assert(gf5_perf_select(&h.ctx,0)==GF5_ENOTSUP && !h.calls[F5_PROG] && h.ctx.phase==GF5_FAULT);
    fresh(); assert(gf5_perf_select(&h.ctx,ENTRY_COUNT)==GF5_EINVAL && !h.nevents);
    assert(gf5_perf_select(0,0)==GF5_EINVAL);
}
void _start(void)
{
    versions(); malformed(); selection();
    __asm__ volatile("int $0x80" : : "a"(1),"b"(0));
    __builtin_unreachable();
}
