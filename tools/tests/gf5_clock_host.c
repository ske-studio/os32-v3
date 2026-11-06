/* B1/C0 fixture: link clock.c and kdebug/gfinit_pll.c, -m32 -nostdlib. */
#define GF5_HOST_UNIT 1
#include "gf5_fake.h"
#include "../../kdebug/gfinit_decode.h"
static struct gf5_host h;
static struct gf4_state state;
static struct gf4_host_event de[64];
static struct gf5_host_event fe[8];
/* Four v0x21 entries, fixed N=20/M=2, 27 MHz reference. */
#define ROM_TABLE 0x5bc8U
#define ROM_ENTRY 31U
static u8 rom[ROM_TABLE+4U+4U*ROM_ENTRY];
static void word(u32 off,u32 value)
{
    rom[off]=(u8)value; rom[off+1]=(u8)(value>>8);
}
static void fixture(void)
{
    gf2_host_memset(rom,0,sizeof(rom));
    rom[ROM_TABLE]=0x21; rom[ROM_TABLE+1]=4;
    rom[ROM_TABLE+2]=ROM_ENTRY; rom[ROM_TABLE+3]=4;
    for (u32 i=0;i<4;i++) {
        u32 p=ROM_TABLE+4+i*ROM_ENTRY;
        word(p+4,200); word(p+6,500);
        word(p+12,10); word(p+16,20);
        rom[p+20]=rom[p+21]=20; rom[p+22]=rom[p+23]=2;
        rom[p+29]=3;
    }
}
/* Register masks and programming sequence: nouveau clk/nv40.c:126-196. */
#define REG_CORE 0x4000U
#define REG_COEF 0x4004U
#define REG_SHADER 0x4008U
#define REG_MEM 0x4020U
#define REG_MUX 0xc040U
/* All distinct registers seeded or written by this fixture. */
static const u32 fixture_regs[] = {
    REG_CORE, REG_COEF, REG_SHADER, REG_MEM, REG_MEM + 4U, REG_MUX
};
static struct gf3_host_reg regs[sizeof(fixture_regs) / sizeof(fixture_regs[0])];
#define OLD_MUX 0xa5a5ffffU
#define MUX_BITS 0x333U
#define CORE_BITS 0xc0070100U
#define SHADER_BITS 0xc007ffffU
#define OLD_CORE (0x3ff8feffU | CORE_BITS)
#define OLD_SHADER (0x3ff80000U | SHADER_BITS)
#define SETTLE_US 5000U /* nouveau clk/nv40.c:193 */
#define REF_KHZ 27000U /* nouveau clk/pllnv04.c:36; fixture reference */
#define N1 20U
#define M1 2U
#define N2 3U
#define M2 2U
#define ENABLE 0x80000000U
#define DOUBLE 0x40000000U
#define SINGLE 0x100U
#define SHARED 0x333U
#define FIXTURE_KHZ (REF_KHZ*N1/M1)
#define OTHER_KHZ (FIXTURE_KHZ+10000U)
static void set_reg(u32 reg,u32 value)
{
    gf3_host_set(&h.display.base,0,reg,value);
}
static void write_event(u32 index,u32 reg,u32 value)
{
    assert(de[index].op==D_WRITE && de[index].a[0]==0 &&
           de[index].a[1]==reg && de[index].a[2]==4 && de[index].a[3]==value);
}
static const struct gf5_clock request={FIXTURE_KHZ,0,FIXTURE_KHZ};
static void calculate(struct gf5_plan *p)
{
    gf5_host_trace_clear(&h);
    assert(gf5_clock_calc(&h.ctx,&request,p)==0);
    struct gf5_host_activity a=gf5_host_activity(&h);
    assert(!a.reads && !a.writes && !a.pauses && !a.resumes);
}
static void reject(const struct gf5_plan *p,i32 rc)
{
    gf5_host_trace_clear(&h);
    assert(gf5_clock_prog(&h.ctx,p)==rc);
    assert(h.ctx.last_rc==rc);
    struct gf5_host_activity a=gf5_host_activity(&h);
    assert(!a.reads && !a.writes && !a.pauses && !a.resumes);
    assert(!h.display.calls[D_SLEEP]);
}
static void guard_cases(void)
{
    struct gf5_plan p,copy,other;
    calculate(&p);
    gf5_plan_invalidate(&h.ctx);
    reject(&p,GF5_ESTATE);
    /* Every public u32 field, including RAM and actual, is authenticated. */
    for (u32 field=0;field<9;field++) {
        calculate(&p); copy=p;
        switch (field) {
        case 0: copy.nc^=1U; break;
        case 1: copy.nn^=1U; break;
        case 2: copy.sc^=1U; break;
        case 3: copy.mux^=1U; break;
        case 4: copy.mc^=1U; break;
        case 5: copy.mn^=1U; break;
        case 6: copy.actual.core_khz^=1U; break;
        case 7: copy.actual.shader_khz^=1U; break;
        case 8: copy.actual.mem_khz^=1U; break;
        }
        reject(&copy,GF5_EINVAL);
        assert(!h.ctx.plan_guard.core_used);
        assert(gf5_plan_check(&h.ctx,&p,false)==0);
    }
    calculate(&p);
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){FIXTURE_KHZ/2U,0,FIXTURE_KHZ},&other)==0);
    assert(p.nc!=other.nc);
    reject(&p,GF5_EINVAL); /* calc B supersedes A. */
    assert(gf5_clock_prog(&h.ctx,&other)==0);
    calculate(&p);
    h.ctx.ch->epoch++;
    reject(&p,GF5_ESTATE);
    calculate(&p);
    h.ctx.rom_generation++;
    reject(&p,GF5_ESTATE); /* Same ROM pointer, new generation. */
    calculate(&p);
    gf5_plan_invalidate(&h.ctx);
    reject(&p,GF5_ESTATE);
    calculate(&p);
    /* Partial/failed calc must discard A and leave the caller's out intact. */
    copy=p;
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){FIXTURE_KHZ,OTHER_KHZ,FIXTURE_KHZ},&copy)==GF5_EBIOS);
    assert(copy.nc==p.nc && copy.nn==p.nn && copy.sc==p.sc && copy.mux==p.mux &&
           copy.mc==p.mc && copy.mn==p.mn && copy.actual.core_khz==p.actual.core_khz &&
           copy.actual.shader_khz==p.actual.shader_khz && copy.actual.mem_khz==p.actual.mem_khz);
    reject(&p,GF5_ESTATE);
    calculate(&p);
    /* A valid ROM edit without a generation change still requires re-solving.
     * v0x21 bias_p changes RAM control, not the core request. */
    rom[ROM_TABLE+4U+30U]=1;
    reject(&p,GF5_EINVAL);
    rom[ROM_TABLE+4U+30U]=0;
    calculate(&p);
    rom[ROM_TABLE]=0;
    reject(&p,GF5_EBIOS);
    assert(h.ctx.source_rc==-1);
    rom[ROM_TABLE]=0x21;
    calculate(&p);
    rom[ROM_TABLE+4U+20U]=rom[ROM_TABLE+4U+21U]=1;
    reject(&p,GF5_ERANGE);
    assert(h.ctx.source_rc==-1);
    fixture();
    calculate(&p); copy=p;
    /* UNIT1 tests only the core boundary: model an already consumed RAM half.
     * The real RAM -> core sequence belongs to U1/integration. */
    h.ctx.plan_guard.ram_used=true;
    assert(gf5_clock_prog(&h.ctx,&copy)==0);
    assert(h.ctx.plan_guard.core_used && h.ctx.plan_guard.ram_used);
    reject(&copy,GF5_ESTATE);
    calculate(&p);
    gf4_host_fail_at(&h.display,D_READ,1,-73);
    assert(gf5_clock_prog(&h.ctx,&p)==GF5_EIO && h.ctx.source_rc==-73);
    assert(h.ctx.plan_guard.core_used && !h.display.calls[D_WRITE]);
    gf4_host_fail_at(&h.display,D_READ,0,0);
    reject(&p,GF5_ESTATE);
    calculate(&p);
    assert(gf5_clock_prog(&h.ctx,&p)==0); /* A fresh calc permits retry. */
}
/* Independent arithmetic from MIT nouveau clk/nv40.c:65-78 and
 * clk/pllnv04.c:86,190-203. No source files are needed at test runtime. */
static void readback_cases(void)
{
    struct gf5_plan p;
    struct gf5_clock c;
    /* 27 MHz * 20 / 7 = 77142 + 6/7 kHz: calc rounds, read truncates. */
    const u32 rounded_khz=77143U, truncated_khz=77142U;
    fixture();
    for (u32 i=0;i<4;i++) {
        u32 at=ROM_TABLE+4U+i*ROM_ENTRY;
        word(at+4,50); word(at+12,1);
        rom[at+22]=rom[at+23]=7;
    }
    gf5_host_trace_clear(&h);
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){rounded_khz,0,rounded_khz},&p)==0);
    assert(p.nc==(ENABLE|SINGLE) && p.nn==((N1<<8)|7U));
    assert(p.actual.core_khz==rounded_khz);
    assert(gf5_clock_prog(&h.ctx,&p)==0);
    assert(gf5_clock_read(&h.ctx,&c)==0);
    assert(c.core_khz==truncated_khz && c.shader_khz==truncated_khz);
    assert(p.actual.core_khz-c.core_khz==1U);
    /* PM's fixedgain2 decision: calc reports stage 1, read multiplies 3/2.
     * Programming remains successful; readback is an observation only. */
    fixture();
    for (u32 i=0;i<4;i++) {
        u32 at=ROM_TABLE+4U+i*ROM_ENTRY;
        word(at+6,300); word(at+8,200); word(at+10,500);
        word(at+14,10); word(at+18,500);
        rom[at+24]=rom[at+25]=N2;
        rom[at+26]=rom[at+27]=M2;
    }
    gf5_host_trace_clear(&h);
    const u32 doubled_khz=405000U; /* 270000 * 3/2, nv40.c:76. */
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){doubled_khz,0,doubled_khz},&p)==0);
    assert(p.nc==(ENABLE|DOUBLE));
    assert(p.nn==((N2<<24)|(M2<<16)|(N1<<8)|M1));
    assert(p.actual.core_khz==FIXTURE_KHZ);
    assert(gf5_clock_prog(&h.ctx,&p)==0);
    assert(gf5_clock_read(&h.ctx,&c)==0);
    assert(c.core_khz==doubled_khz && c.shader_khz==doubled_khz);
    fixture();
}
static void run(void)
{
    struct gf5_plan p;
    struct gf5_clock c;
    gf5_host_init(&h,&state);
    h.display.base.regs=regs;
    h.display.base.cregs=sizeof(regs)/sizeof(regs[0]);
    h.display.events=de; h.display.cevents=64;
    h.events=fe; h.cevents=8;
    fixture(); h.ctx.rom.bytes=rom; h.ctx.rom.size=sizeof(rom);
    p=(struct gf5_plan){0};
    reject(&p,GF5_ESTATE); /* Cold fixture has never called calc. */
    calculate(&p);
    set_reg(REG_MUX,OLD_MUX); set_reg(REG_CORE,OLD_CORE);
    set_reg(REG_SHADER,OLD_SHADER);
    assert(OLD_CORE & CORE_BITS & ~p.nc);
    assert(OLD_SHADER & SHADER_BITS & ~p.sc);
    assert(gf5_clock_prog(&h.ctx,&p)==0);
    assert(h.display.calls[D_WRITE]==5);
    write_event(1,REG_MUX,OLD_MUX&~MUX_BITS);
    write_event(2,REG_COEF,p.nn);
    write_event(4,REG_CORE,(OLD_CORE&~CORE_BITS)|p.nc);
    write_event(6,REG_SHADER,(OLD_SHADER&~SHADER_BITS)|p.sc);
    u32 elapsed=0, index=7;
    while (index<h.display.nevents && de[index].op==D_SLEEP) {
        assert(de[index].a[0]==SETTLE_US);
        elapsed+=de[index++].a[0];
    }
    assert(elapsed==SETTLE_US && h.display.calls[D_SLEEP]==index-7);
    assert(de[index].op==D_READ && de[index].a[1]==REG_MUX);
    write_event(index+1,REG_MUX,(OLD_MUX&~MUX_BITS)|p.mux);
    assert(h.display.nevents==index+2);
    gf5_host_trace_clear(&h);
    calculate(&p);
    gf4_host_fail_at(&h.display,D_WRITE,2,-1);
    assert(gf5_clock_prog(&h.ctx,&p)==GF5_EIO && h.ctx.source_rc==-1);
    assert(h.display.calls[D_WRITE]==2 && !h.display.calls[D_SLEEP]);
    gf4_host_fail_at(&h.display,D_WRITE,0,0);
    reject(&p,GF5_ESTATE);
    gf5_host_trace_clear(&h);
    calculate(&p);
    gf4_host_fail_at(&h.display,D_SLEEP,1,-1);
    assert(gf5_clock_prog(&h.ctx,&p)==GF5_EIO && h.ctx.source_rc==-1);
    assert(h.display.calls[D_SLEEP]==1 && h.display.calls[D_WRITE]==4);
    gf4_host_fail_at(&h.display,D_SLEEP,0,0);
    reject(&p,GF5_ESTATE);
    gf5_host_trace_clear(&h);
    assert(gf5_clock_read(&h.ctx,&c)==0);
    assert(!c.core_khz && !c.shader_khz && !c.mem_khz);
    /* Independent two-stage read fixture, nv40.c:39-74: 27 MHz * 20/2 * 3/2. */
    set_reg(REG_MUX,SHARED);
    set_reg(REG_CORE,ENABLE|DOUBLE);
    set_reg(REG_COEF,(N2<<24)|(M2<<16)|(N1<<8)|M1);
    set_reg(REG_MEM,ENABLE|DOUBLE);
    set_reg(REG_MEM+4,(N2<<24)|(M2<<16)|(N1<<8)|M1);
    gf5_host_trace_clear(&h);
    assert(gf5_clock_read(&h.ctx,&c)==0);
    assert(c.core_khz==REF_KHZ*N1/M1*N2/M2 &&
           c.shader_khz==REF_KHZ*N1/M1*N2/M2 && c.mem_khz==REF_KHZ*N1/M1*N2/M2);
    gf5_host_trace_clear(&h);
    gf4_host_fail_at(&h.display,D_READ,2,-1);
    c=(struct gf5_clock){1,2,3};
    assert(gf5_clock_read(&h.ctx,&c)==GF5_EIO && h.ctx.source_rc==-1);
    assert(h.display.calls[D_READ]==2 && c.core_khz==1 && c.shader_khz==2 && c.mem_khz==3);
    gf4_host_fail_at(&h.display,D_READ,0,0);
    h.ctx.rom.bytes=0;
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){FIXTURE_KHZ,0,FIXTURE_KHZ},&p)==GF5_ENOTSUP);
    fixture(); h.ctx.rom.bytes=rom; h.ctx.rom.size=sizeof(rom);
    rom[ROM_TABLE]=0;
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){FIXTURE_KHZ,0,FIXTURE_KHZ},&p)==GF5_EBIOS && h.ctx.source_rc==-1);
    rom[ROM_TABLE]=0x21;
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){U32_MAX,0,FIXTURE_KHZ},&p)==GF5_ERANGE && h.ctx.source_rc==-1);
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){FIXTURE_KHZ,0,FIXTURE_KHZ},&p)==0);
    assert(p.nc==(ENABLE|SINGLE));
    assert(p.nn==((N1<<8)|M1) && p.sc==0 && p.mux==SHARED);
    assert(p.mc==(ENABLE|SINGLE) && p.mn==((N1<<8)|M1));
    assert(p.actual.core_khz==REF_KHZ*N1/M1 && p.actual.shader_khz==REF_KHZ*N1/M1 && p.actual.mem_khz==REF_KHZ*N1/M1);
    gf5_host_trace_clear(&h);
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){FIXTURE_KHZ,OTHER_KHZ,FIXTURE_KHZ},&p)==GF5_EBIOS);
    assert(!h.display.calls[D_WRITE]);
    fixture(); h.ctx.rom.bytes=rom; h.ctx.rom.size=sizeof(rom);
    calculate(&p);
    h.ctx.heat.down_active=true;
    h.ctx.heat.ceiling=(struct gf5_clock){1,1,1};
    gf5_host_trace_clear(&h);
    assert(gf5_clock_prog(&h.ctx,&p)==GF5_EBUSY);
    assert(!h.display.nevents && !h.ctx.plan_guard.core_used);
    h.ctx.heat.down_active=false;
    guard_cases();
    readback_cases();
}
void _start(void)
{
    run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    __builtin_unreachable();
}
