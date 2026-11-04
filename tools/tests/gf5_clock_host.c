/* B1/C0 fixture: link clock.c and kdebug/gfinit_pll.c, -m32 -nostdlib. */
#define GF5_HOST_UNIT 1
#include "gf5_fake.h"
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
static void run(void)
{
    struct gf5_plan p={.nc=0x80000100U,.nn=0x1402U,.sc=0,.mux=0x333U};
    struct gf5_clock c;
    gf5_host_init(&h,&state);
    h.display.events=de; h.display.cevents=64;
    h.events=fe; h.cevents=8;
    assert(gf5_clock_prog(&h.ctx,&p)==0);
    assert(h.display.calls[D_WRITE]==5 && h.display.calls[D_SLEEP]==1);
    assert(de[1].op==D_WRITE && de[1].a[1]==0xc040U);
    assert(de[2].op==D_WRITE && de[2].a[1]==0x4004U);
    assert(de[4].op==D_WRITE && de[4].a[1]==0x4000U);
    assert(de[6].op==D_WRITE && de[6].a[1]==0x4008U);
    assert(de[7].op==D_SLEEP && de[7].a[0]==5000U);
    assert(de[9].op==D_WRITE && de[9].a[1]==0xc040U);
    gf5_host_trace_clear(&h);
    gf4_host_fail_at(&h.display,D_WRITE,2,-1);
    assert(gf5_clock_prog(&h.ctx,&p)==GF5_EIO);
    assert(h.display.calls[D_WRITE]==2 && !h.display.calls[D_SLEEP]);
    gf4_host_fail_at(&h.display,D_WRITE,0,0);
    gf5_host_trace_clear(&h);
    assert(gf5_clock_read(&h.ctx,&c)==GF5_ESTATE);
    assert(c.core_khz==270000U && c.shader_khz==270000U && !c.mem_khz);
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){270000,0,270000},&p)==GF5_EBIOS);
    fixture(); h.ctx.rom.bytes=rom; h.ctx.rom.size=sizeof(rom);
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){270000,0,270000},&p)==0);
    assert(p.nc==0x80000100U && p.nn==0x1402U && p.sc==0 && p.mux==0x333U);
    assert(p.mc==0x80000100U && p.mn==0x1402U);
    assert(p.actual.core_khz==270000U && p.actual.shader_khz==270000U && p.actual.mem_khz==270000U);
    gf5_host_trace_clear(&h);
    assert(gf5_clock_calc(&h.ctx,&(struct gf5_clock){270000,280000,270000},&p)==GF5_EBIOS);
    assert(!h.display.calls[D_WRITE]);
}
void _start(void)
{
    run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    __builtin_unreachable();
}
