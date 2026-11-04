/* Link hwdev/gf/perf.c, UNIT 0, freestanding (-nostdlib).
 * Golden ROM fields are independently transcribed from MIT nouveau c03c1a1
 * nvkm/subdev/bios/perf.c:30-145; the signed NV4a example is 325-25 MHz,
 * memory 200 MHz * 2000 = 400000 kHz. No installed refs are read at runtime.
 * Call oracle: nvkm/subdev/clk/base.c:159-215,266-305 plus GF_FEAT5_DESIGN
 * §5 (stop on error), final shared adapter contract, and PM decision 15.
 * Observe only seeds dependency outputs; all stubs belong to gf5_fake.h. */
#define GF5_HOST_UNIT 0
#include "gf5_fake.h"

enum {
    ROM_BYTES=1024, BIT_AT=0x20, BIT_HEAD=12, BIT_STRIDE=6,
    POWER_AT=0x60, PERF_AT=0x180, PERF_HEAD=8, ENTRY_LEN=0x40,
    SUB_COUNT=2, SUB_LEN=3, ENTRY_STRIDE=ENTRY_LEN+SUB_COUNT*SUB_LEN,
    BMP_AT=0x80, BMP_PTR=0x94, BMP_HEADER=4,
    TRACE_CAP=64, OUTER_DL=123456,
    KEEP_COUNT=0xabcdef
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
    .actual={300000,300000,400000}
};
static bool saw_ram,saw_core;
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
    if (e->rc) {
        if (lower_source) h.ctx.source_rc=lower_source;
        return;
    }
    if (e->op==F5_CALC) {
        assert(e->a[0]==F5_CALL_CALC);
        const struct gf5_clock *request=e->p[1];
        assert(same_clock(*request,(struct gf5_clock){300000,300000,400000}));
        *(struct gf5_plan *)e->p[2]=planned;
    }
    if (e->op==F5_RAM || e->op==F5_PROG) {
        const struct gf5_plan *p=e->p[1];
        assert(p->nc==planned.nc && p->nn==planned.nn && p->sc==planned.sc &&
            p->mux==planned.mux && p->mc==planned.mc && p->mn==planned.mn &&
            same_clock(p->actual,planned.actual));
        if (e->op==F5_RAM) {
            assert(h.display.base.paused && !h.ctx.accepting); saw_ram=true;
        } else {
            assert(saw_ram && !h.display.base.paused && h.ctx.accepting); saw_core=true;
        }
    }
    if (e->op==F5_FAN) {
        assert(e->a[0]==F5_CALL_CSTATE && e->a[1]==60);
        /* The fixture intentionally leaves policy updates to observation. */
        if ((i32)e->a[2]>0) h.ctx.fan.cstate=60;
    }
}
static void fresh(void)
{
    gf2_host_memset(&state,0,sizeof(state));
    gf5_host_init(&h,&state);
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
    rom[PERF_AT]=0x21; rom[PERF_AT+1]=PERF_HEAD; rom[PERF_AT+2]=2;
    rom[PERF_AT+3]=ENTRY_LEN; rom[PERF_AT+4]=SUB_COUNT; rom[PERF_AT+5]=SUB_LEN;
    for (u32 i=0;i<2;++i) {
        u32 at=PERF_AT+PERF_HEAD+i*ENTRY_STRIDE;
        rom[at+4]=(u8)(60+i); rom[at+5]=(u8)(7+i);
        put16(at+6,300+i*25); rom[at+8]=i ? (u8)-25 : 0;
        put16(at+0x0b,200+i*10);
    }
    saw_ram=saw_core=false; lower_source=0;
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
        u32 n=0; assert(!gf5_perf_count(&h.ctx,&n) && n==2);
        expect_get(0,(struct gf5_perf_entry){{300000,300000,400000},7,60});
        expect_get(1,(struct gf5_perf_entry){{300000,325000,420000},8,61});
    }
    static const u8 newer[]={0x25,0x30,0x35};
    for (u32 i=0;i<sizeof(newer);++i) {
        fresh(); rom[PERF_AT]=newer[i];
        u32 at=PERF_AT+PERF_HEAD;
        gf2_host_memset(rom+at,0,ENTRY_LEN);
        u32 fan=i ? 6 : 4, core=i ? 8 : 6;
        rom[at+fan]=55; rom[at+fan+1]=9;
        put16(at+core,275); put16(at+0x0a,300); put16(at+0x0c,350);
        expect_get(0,(struct gf5_perf_entry){{275000,300000,350000},9,55});
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
        put32(at+1,30000); put32(at+5,20000);
        rom[at+0x37]=70; rom[at+0x38]=11;
        expect_get(0,(struct gf5_perf_entry){{300000,0,400000},11,70});
        rom[PERF_AT+3]=0x38; /* Voltage is optional in these versions. */
        expect_get(0,(struct gf5_perf_entry){{300000,0,400000},0,70});
        put32(at+1,0xffffffffU);
        struct gf5_perf_entry e={0};
        assert(gf5_perf_get(&h.ctx,0,&e)==GF5_EBIOS && !e.clock.core_khz);
    }
}
static void malformed(void)
{
    u32 n;
    struct gf5_perf_entry keep={{1,2,3},4,5},e;
    fresh(); e=keep;
    assert(gf5_perf_get(&h.ctx,2,&e)==GF5_EINVAL && same_entry(e,keep));
    assert(gf5_perf_count(0,&n)==GF5_EINVAL);
    assert(gf5_perf_count(&h.ctx,0)==GF5_EINVAL);
    assert(gf5_perf_get(&h.ctx,0,0)==GF5_EINVAL);
    fresh(); h.ctx.rom.bytes=0; n=KEEP_COUNT;
    assert(gf5_perf_count(&h.ctx,&n)==GF5_ENOTSUP && n==KEEP_COUNT);
    for (u32 size=0;size<PERF_HEAD+2*ENTRY_STRIDE;++size) {
        fresh(); h.ctx.rom.size=PERF_AT+size; n=KEEP_COUNT;
        assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS && n==KEEP_COUNT);
        e=keep; assert(gf5_perf_get(&h.ctx,0,&e)==GF5_EBIOS && same_entry(e,keep));
    }
    fresh(); h.ctx.rom.size=PERF_AT+PERF_HEAD+2*ENTRY_STRIDE;
    assert(!gf5_perf_count(&h.ctx,&n) && n==2); /* Exact full-span control. */
    fresh(); put32(POWER_AT,0xffffffffU);
    assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS);
    fresh(); put16(BIT_AT+BIT_HEAD+2,3);
    assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS);
    fresh(); rom[BIT_AT+9]=BIT_STRIDE-1;
    assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS);
    fresh(); rom[PERF_AT+1]=PERF_HEAD-3;
    assert(gf5_perf_count(&h.ctx,&n)==GF5_EBIOS);
    fresh(); rom[PERF_AT+3]=0x0c;
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
    {F5_CALC,0},{F5_PAUSE,0},{F5_RAM,0},{F5_RESUME,0},{F5_ADMIT,1},
    {F5_FAN,1},{F5_VOLT,1},{F5_PROG,0},{F5_VOLT,-1},{F5_FAN,-1}
};
static void prefix(u32 count)
{
    assert(h.nevents>=count);
    for (u32 i=0;i<count;++i) {
        const struct gf5_host_event *e=&events[i];
        assert(e->op==golden[i].op && e->p[0]==&h.ctx);
        if (e->op==F5_VOLT) {
            assert(e->a[0]==F5_CALL_ID && e->a[1]==7 && e->a[2]==7 &&
                (i32)e->a[3]==golden[i].direction);
        }
        if (e->op==F5_FAN) assert((i32)e->a[2]==golden[i].direction);
        if (e->op==F5_ADMIT) assert(e->a[0]==1);
        if (e->op==F5_PAUSE) assert(e->a[0]==OUTER_DL);
    }
}
static void selection(void)
{
    const u32 count=sizeof(golden)/sizeof(golden[0]);
    fresh();
    assert(!gf5_perf_select(&h.ctx,0)); prefix(count);
    assert(h.nevents==count && saw_ram && saw_core && h.ctx.accepting &&
        h.ctx.phase==GF5_RUN && !h.display.base.paused);
    assert(!h.ctx.last_rc && !h.ctx.source_rc && h.ctx.fan.cstate==60);
    /* Every injected stage must stop at the failing call, then only close
     * admission. No later voltage/fan lowering, resume or program is allowed. */
    for (u32 i=0;i<count;++i) {
        fresh();
        u32 nth=1;
        for (u32 j=0;j<i;++j) if (golden[j].op==golden[i].op) ++nth;
        gf5_host_fail_at(&h,golden[i].op,nth,GF5_EIO);
        assert(gf5_perf_select(&h.ctx,0)==GF5_EIO);
        prefix(i+1);
        assert(h.ctx.last_rc==GF5_EIO && h.ctx.source_rc==GF5_EIO);
        if (!i) {
            assert(h.nevents==1 && h.ctx.phase==GF5_RUN && h.ctx.accepting);
        } else {
            assert(h.nevents==i+2 && events[i+1].op==F5_ADMIT && !events[i+1].a[0]);
            assert(h.ctx.phase==GF5_FAULT && !h.ctx.accepting);
            if (i<=3) assert(h.display.base.paused);
        }
        assert(h.ctx.image==&image && h.ctx.rom.bytes==rom &&
            h.ctx.rom.size==sizeof(rom) && !h.live_allocs && h.ctx.deadline==OUTER_DL);
    }
    /* EBIOS from calc (including Ra266 separate shader) never pauses. */
    fresh(); gf5_host_fail_at(&h,F5_CALC,1,GF5_EBIOS);
    assert(gf5_perf_select(&h.ctx,0)==GF5_EBIOS && h.nevents==1 && h.ctx.phase==GF5_RUN);
    /* Lower callback error and original code survive even failed closure. */
    fresh(); gf5_host_fail_at(&h,F5_PAUSE,1,GF2_ETIME);
    gf5_host_fail_at(&h,F5_ADMIT,1,GF4_EINVAL);
    assert(gf5_perf_select(&h.ctx,0)==GF5_ETIME && h.ctx.source_rc==GF2_ETIME);
    assert(h.ctx.last_rc==GF5_ETIME && h.ctx.phase==GF5_FAULT && !h.ctx.accepting);
    fresh(); lower_source=GF2_EIO; gf5_host_fail_at(&h,F5_PROG,1,GF5_EIO);
    assert(gf5_perf_select(&h.ctx,0)==GF5_EIO && h.ctx.source_rc==GF2_EIO);
    /* ENOTSUP is optional only at fan/voltage boundaries, with visible rc. */
    fresh(); gf5_host_fail_at(&h,F5_FAN,0,GF5_ENOTSUP);
    gf5_host_fail_at(&h,F5_VOLT,0,GF5_ENOTSUP);
    assert(!gf5_perf_select(&h.ctx,0)); prefix(count);
    assert(h.nevents==count && h.ctx.last_rc==GF5_ENOTSUP &&
        h.ctx.source_rc==GF5_ENOTSUP && h.ctx.phase==GF5_RUN);
    fresh(); gf5_host_fail_at(&h,F5_RAM,1,GF5_ENOTSUP);
    assert(gf5_perf_select(&h.ctx,0)==GF5_ENOTSUP && !h.calls[F5_PROG] && h.ctx.phase==GF5_FAULT);
    fresh(); h.ctx.links.unpause=0;
    assert(gf5_perf_select(&h.ctx,0)==GF5_EINVAL && !h.nevents);
    fresh(); assert(gf5_perf_select(&h.ctx,2)==GF5_EINVAL && !h.nevents);
    assert(gf5_perf_select(0,0)==GF5_EINVAL);
}
void _start(void)
{
    versions(); malformed(); selection();
    __asm__ volatile("int $0x80" : : "a"(1),"b"(0));
    __builtin_unreachable();
}
