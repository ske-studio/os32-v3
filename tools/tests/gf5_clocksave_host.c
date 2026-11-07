/* Real clock.c + state.c: observed NV4a snapshot from 1eb471a0,
 * 2026-11-06 gf-obs509. Expected arithmetic/zero policy: MIT nouveau
 * c03c1a1 clk/nv40.c:39-115,146-182 and clk/base.c:604-618.
 * Reproduce by seeding these six BAR0 values; no external refs required.
 * RAM/voltage/display remain the shared UNIT6 boundaries. */
#define GF5_HOST_UNIT 6
#define GF5_HOST_REAL_CLOCK
#include "gf5_fake.h"
#include "../../kdebug/gfinit_pll.h"
#include "../../kdebug/gfinit_decode.h"

#define CORE_KHZ 200000U
#define MEM_KHZ 400000U
#define UV 1200000U
#define TRACE_CAP 128U /* zero extents, 12 tiles, finite state boundaries */
#define ROM_ENTRY 31U
static struct gf5_host h;
static struct gf4_state display;
static struct gf5_host_event trace[TRACE_CAP];
static struct gf4_host_event dtrace[TRACE_CAP];
static struct gf3_host_reg regs[6];
static u8 arena[sizeof(struct gf5_image)+GF5_HOST_ALLOC_ALIGN*2U]
    __attribute__((aligned(GF5_HOST_ALLOC_ALIGN)));
static u8 rom[GF_INIT_PLL_TABLE+4U+4U*ROM_ENTRY];
static const struct { u32 reg,value; } observed[]={
    {0xc040U,0x3c0bc003U}, {0x4000U,0xc002001cU},
    {0x4004U,0x14032809U}, {0x4008U,0xc000001cU},
    {0x4020U,0xe041001cU}, {0x4024U,0x14032809U}
};
static void word(u32 at,u32 value)
{
    rom[at]=(u8)value; rom[at+1]=(u8)(value>>8);
}
static void pll_fixture(void)
{
    /* Synthetic wide limits, not the measured ROM: permit exact 200/400 MHz
     * plans while exercising the production parser and integer solver. */
    rom[GF_INIT_PLL_TABLE]=0x21; rom[GF_INIT_PLL_TABLE+1]=4;
    rom[GF_INIT_PLL_TABLE+2]=ROM_ENTRY; rom[GF_INIT_PLL_TABLE+3]=4;
    for (u32 i=0;i<4;++i) {
        u32 at=GF_INIT_PLL_TABLE+4U+i*ROM_ENTRY;
        word(at+GF_PLL_ENT_VCO1_MIN,100); word(at+GF_PLL_ENT_VCO1_MAX,500);
        word(at+GF_PLL_ENT_VCO2_MIN,400); word(at+GF_PLL_ENT_VCO2_MAX,1000);
        word(at+GF_PLL_ENT_VCO1_INPUT_MIN,1); word(at+GF_PLL_ENT_VCO1_INPUT_MAX,27);
        word(at+GF_PLL_ENT_VCO2_INPUT_MIN,1); word(at+GF_PLL_ENT_VCO2_INPUT_MAX,500);
        rom[at+GF_PLL_ENT_VCO1_N_MIN]=1; rom[at+GF_PLL_ENT_VCO1_N_MAX]=255;
        rom[at+GF_PLL_ENT_VCO1_M_MIN]=1; rom[at+GF_PLL_ENT_VCO1_M_MAX]=31;
        rom[at+GF_PLL_ENT_VCO2_N_MIN]=4; rom[at+GF_PLL_ENT_VCO2_N_MAX]=30;
        rom[at+GF_PLL_ENT_VCO2_M_MIN]=1; rom[at+GF_PLL_ENT_VCO2_M_MAX]=4;
        rom[at+GF_PLL_ENT_MAX_P]=7;
    }
    h.ctx.rom.bytes=rom; h.ctx.rom.size=sizeof(rom);
}
static void observe(struct gf5_host *host,const struct gf5_host_event *e)
{
    (void)host;
    if (e->rc) return;
    if (e->op==F5_VOLT && e->a[0]==F5_CALL_GET) *(u32 *)e->p[1]=UV;
    if (e->op==F5_RAM) {
        const struct gf5_plan *p=e->p[1];
        assert(p->actual.core_khz==CORE_KHZ && p->actual.mem_khz==MEM_KHZ);
        assert(p->actual.shader_khz==CORE_KHZ && p->mux==0x333U && !p->sc);
    }
}
static void reset(void)
{
    gf2_host_memset(&display,0,sizeof(display));
    gf5_host_init(&h,&display);
    h.events=trace; h.cevents=TRACE_CAP; h.observe=observe;
    h.display.events=dtrace; h.display.cevents=TRACE_CAP;
    h.display.base.regs=regs; h.display.base.cregs=6;
    gf5_host_arena(&h,arena,sizeof(arena));
    for (u32 i=0;i<6;++i)
        gf3_host_set(&h.display.base,0,observed[i].reg,observed[i].value);
    pll_fixture();
}
static void readback(void)
{
    struct gf5_clock c;
    reset();
    assert(gf5_clock_read(&h.ctx,&c)==0);
    assert(c.core_khz==CORE_KHZ && !c.shader_khz && c.mem_khz==MEM_KHZ);
    assert(!h.ctx.source_rc && !h.ctx.last_rc && !h.display.calls[D_WRITE]);
    /* Both unimplemented mux sources return zero successfully. */
    for (u32 src=0;src<2;++src) {
        gf3_host_set(&h.display.base,0,observed[0].reg,src|(src<<4));
        assert(gf5_clock_read(&h.ctx,&c)==0);
        assert(!c.core_khz && !c.shader_khz && c.mem_khz==MEM_KHZ);
    }
    /* Single PLL source, independent 27 MHz * 20/2 >> 1 = 135 MHz. */
    gf3_host_set(&h.display.base,0,observed[0].reg,0x22U);
    gf3_host_set(&h.display.base,0,0x4008U,0xc0011402U);
    assert(gf5_clock_read(&h.ctx,&c)==0);
    assert(c.core_khz==135000U && c.shader_khz==135000U);
}
static void roundtrip(void)
{
    reset();
    struct gf5_image *image=0;
    assert(gf5_state_save(&h.ctx,&image)==0);
    assert(image && image->complete && h.ctx.phase==GF5_SAVED);
    assert(image->version==3U);
    assert(image->clock.core_khz==CORE_KHZ && !image->clock.shader_khz &&
           image->clock.mem_khz==MEM_KHZ && image->requested_uv==UV);
    gf5_host_trace_clear(&h);
    assert(gf5_state_restore(&h.ctx,image)==0);
    assert(h.ctx.phase==GF5_RUN && h.ctx.accepting && !h.ctx.busy);
    assert(h.calls[F5_RAM]==1 && h.display.calls[D_WRITE]==5);
    struct gf5_clock c;
    assert(gf5_clock_read(&h.ctx,&c)==0);
    assert(c.core_khz==CORE_KHZ && c.shader_khz==CORE_KHZ);
    gf5_state_free(&h.ctx,image);
    assert(!h.ctx.image && !h.live_allocs && h.calls[F5_FREE]==1);
}
static void optional_image_contract(void)
{
    /* Declaration control for S/C: fan absent and voltage unknown can
     * coexist with an acquired clock whose shader source is absent (0).
     * Raw values retain the observed mux, independently of frequency calc. */
    struct gf5_image image={.clock={CORE_KHZ,0,MEM_KHZ},
        .clock_available=true,.clock_raw={0xc002001cU,0x14032809U,
                                         0xc000001cU,0x3c0bc003U}};
    struct gf5_image copy=image;
    assert(!copy.fan_available && !copy.volt_available && copy.clock_available);
    assert(!copy.clock.shader_khz && copy.clock_raw.nc==observed[1].value &&
           copy.clock_raw.nn==observed[2].value &&
           copy.clock_raw.sc==observed[3].value &&
           copy.clock_raw.mux==observed[0].value);
    image=(struct gf5_image){.fan_available=true,.volt_available=true};
    assert(image.fan_available && image.volt_available && !image.clock_available);
}
static void raw_roundtrip(void)
{
    struct gf5_clock_raw raw;
    reset();
    /* No BIOS, plan or frequency is needed for a boot snapshot. */
    h.ctx.rom.bytes=0;
    assert(!gf5_clock_raw_save(&h.ctx,&raw));
    assert(raw.nc==observed[1].value && raw.nn==observed[2].value &&
           raw.sc==observed[3].value && raw.mux==observed[0].value);
    assert(h.display.calls[D_READ]==4 && !h.display.calls[D_WRITE] &&
           !h.display.calls[D_SLEEP]);
    for (u32 i=0;i<4;++i)
        gf3_host_set(&h.display.base,0,observed[i].reg,~observed[i].value);
    gf5_host_trace_clear(&h);
    assert(!gf5_clock_raw_restore(&h.ctx,&raw));
    /* nv40_clk_prog: disconnect, coefficient, core, shader, 5ms, mux.
     * OS32 restores full words, including bits outside the calc masks. */
    const u32 order[]={0,2,1,3,0};
    u32 writes=0,waited=0;
    for (u32 i=0;i<h.display.nevents;++i) {
        const struct gf4_host_event *e=&dtrace[i];
        if (e->op==D_WRITE) {
            assert(writes<5 && e->a[0]==0 && e->a[2]==4);
            u32 at=order[writes];
            assert(e->a[1]==observed[at].reg);
            assert(e->a[3]==(writes ? observed[at].value :
                                      (~raw.mux & ~0x333U)));
            assert(writes==4 ? waited==5000U : waited==0);
            ++writes;
        } else if (e->op==D_SLEEP) {
            assert(writes==4);
            waited+=e->a[0];
        }
    }
    assert(writes==5 && waited==5000U);
    struct gf5_clock_raw copy;
    assert(!gf5_clock_raw_save(&h.ctx,&copy));
    assert(copy.nc==raw.nc && copy.nn==raw.nn && copy.sc==raw.sc && copy.mux==raw.mux);
    /* All-zero raw words are also a valid snapshot, with no PLL checks. */
    raw=(struct gf5_clock_raw){0};
    assert(!gf5_clock_raw_restore(&h.ctx,&raw));
    assert(!gf5_clock_raw_save(&h.ctx,&copy));
    assert(!copy.nc && !copy.nn && !copy.sc && !copy.mux);
}
static void raw_errors(void)
{
    for (u32 nth=1;nth<=4;++nth) {
        reset();
        struct gf5_clock_raw raw={1,2,3,4};
        gf4_host_fail_at(&h.display,D_READ,nth,-73);
        assert(gf5_clock_raw_save(&h.ctx,&raw)==GF5_EIO && h.ctx.source_rc==-73);
        assert(raw.nc==1 && raw.nn==2 && raw.sc==3 && raw.mux==4);
        assert(h.display.calls[D_READ]==nth && !h.display.calls[D_WRITE]);
    }
    const struct gf5_clock_raw raw={0xc002001cU,0x14032809U,0xc000001cU,0x3c0bc003U};
    for (u32 nth=1;nth<=5;++nth) {
        reset();
        gf4_host_fail_at(&h.display,D_WRITE,nth,-73);
        assert(gf5_clock_raw_restore(&h.ctx,&raw)==GF5_EIO && h.ctx.source_rc==-73);
        assert(h.display.calls[D_WRITE]==nth);
        assert(h.display.calls[D_SLEEP]==(nth==5 ? 1U : 0U));
    }
    reset();
    gf4_host_fail_at(&h.display,D_READ,1,-73);
    assert(gf5_clock_raw_restore(&h.ctx,&raw)==GF5_EIO && h.ctx.source_rc==-73);
    assert(!h.display.calls[D_WRITE]);
    reset();
    gf4_host_fail_at(&h.display,D_SLEEP,1,-73);
    assert(gf5_clock_raw_restore(&h.ctx,&raw)==GF5_EIO && h.ctx.source_rc==-73);
    assert(h.display.calls[D_WRITE]==4 && h.display.calls[D_SLEEP]==1);
}
_Static_assert(__builtin_types_compatible_p(__typeof__(&gf5_clock_raw_save),
    i32 (*)(struct gf5 *,struct gf5_clock_raw *)),"raw save boundary");
_Static_assert(__builtin_types_compatible_p(__typeof__(&gf5_clock_raw_restore),
    i32 (*)(struct gf5 *,const struct gf5_clock_raw *)),"raw restore boundary");
void _start(void)
{
    raw_roundtrip();
    raw_errors();
    optional_image_contract();
    roundtrip();
    readback();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    __builtin_unreachable();
}
