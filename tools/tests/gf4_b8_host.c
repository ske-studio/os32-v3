/* Oracle: nouveau c03c1a1 dispnv04/crtc.c:778-797, hw.c:795-810,
 * hw.h:194-200, nvreg.h:92,129-139,433-437. */
#define GF4_HOST_UNIT 8
#include "gf4_display_fake.h"
#define GF_PRODUCTION_MODULE
#include "../../hwdev/gf/display/palette.c"
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
#define T_ENTRIES 256U
#define T_RGB 3U
#define T_MASK 0x6813c6U
#define T_MASK_VALUE 0xffU
#define T_GATE_VALUE 0x20U
#define T_DATA_WRITES (T_ENTRIES * T_RGB)
#define T_WRITES (2U + T_DATA_WRITES + 1U)
#define T_FAIL_WRITE 19U
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_host_event events[T_WRITES + 8U];
static struct gf3_host_reg registers[8];
static u16 ramp[T_RGB][T_ENTRIES];
static u8 palette[T_DATA_WRITES];
static struct gf_display_context legacy;

static int sequence(u8 head, bool indexed)
{
    u32 delta=head*GF4_HOST_STRIDE;
    REQUIRE(host.nevents==T_WRITES+1U);
    REQUIRE(events[0].op==D_WRITE && events[0].a[1]==T_MASK+delta && events[0].a[3]==T_MASK_VALUE);
    REQUIRE(events[1].op==D_WRITE && events[1].a[1]==GF4_HOST_DAC_INDEX+delta && events[1].a[3]==0);
    for (u32 i=0;i<T_DATA_WRITES;++i) {
        const struct gf4_host_event *e=&events[2U+i];
        u32 expected=indexed ? palette[i] : ramp[i%T_RGB][i/T_RGB]/256U;
        REQUIRE(e->op==D_WRITE && e->a[0]==GF_MODE_REGISTER_BAR && e->a[1]==GF4_HOST_DAC_DATA+delta && e->a[2]==1 && e->a[3]==expected);
        REQUIRE(host.lut[head][i]==expected);
    }
    REQUIRE(events[2U+T_DATA_WRITES].op==D_READ && events[2U+T_DATA_WRITES].a[1]==GF4_HOST_STATUS+delta);
    REQUIRE(events[3U+T_DATA_WRITES].op==D_WRITE && events[3U+T_DATA_WRITES].a[1]==GF4_HOST_AR+delta && events[3U+T_DATA_WRITES].a[3]==T_GATE_VALUE);
    return 0;
}
static int test(void)
{
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=sizeof(events)/sizeof(events[0]);
    host.base.regs=registers; host.base.cregs=sizeof(registers)/sizeof(registers[0]);
    for (u32 i=0;i<T_ENTRIES;++i) {
        ramp[0][i]=(u16)(i*257U); /* identity, crtc.c:778-797 */
        ramp[1][i]=(u16)((255U-i)*257U);
        ramp[2][i]=(u16)((i^0x55U)*257U);
        for (u32 c=0;c<T_RGB;++c) palette[i*T_RGB+c]=(u8)(i+c*17U);
    }
    struct gf4_head_image *h=&state.head[1];
    h->mode.fmt=GF3_RGB565;
    REQUIRE(!gf4_gamma(&host.ctx,1,ramp) && h->lut_dirty && !host.nevents);
    h->enabled=true;
    REQUIRE(!gf4_lut_load(&host.ctx,1) && !h->lut_dirty && !sequence(1,false));
    gf4_host_trace_clear(&host);
    h->mode.fmt=GF3_XRGB8888;
    REQUIRE(!gf4_gamma(&host.ctx,1,ramp) && !sequence(1,false));
    gf4_host_trace_clear(&host);
    gf4_host_fail_at(&host,D_WRITE,T_FAIL_WRITE,GF2_EIO);
    ramp[0][0]=0xff00U;
    REQUIRE(gf4_gamma(&host.ctx,1,ramp)==GF2_EIO && h->lut_dirty && h->gamma[0][0]==0xff00U);
    REQUIRE(host.calls[D_WRITE]==T_FAIL_WRITE && !host.calls[D_READ]);
    gf4_host_fail_at(&host,D_WRITE,0,0);
    gf4_host_trace_clear(&host);
    REQUIRE(!gf4_lut_load(&host.ctx,1) && !h->lut_dirty && !sequence(1,false));
    gf4_host_trace_clear(&host);
    h->mode.fmt=GF3_INDEX8;
    REQUIRE(gf4_gamma(&host.ctx,1,ramp)==GF4_EFORMAT && !host.nevents);
    REQUIRE(!gf4_palette_set(&host.ctx,1,0,T_ENTRIES,palette) && !sequence(1,true));
    gf4_host_trace_clear(&host);
    host.ctx.changing=true;
    state.next[1]=*h; state.next[1].mode.fmt=GF3_RGB565;
    REQUIRE(!gf4_lut_load(&host.ctx,1) && !sequence(1,false));
    REQUIRE(state.head[1].mode.fmt==GF3_INDEX8);
    gf4_host_trace_clear(&host);
    REQUIRE(gf4_palette_set(&host.ctx,1,0,1,palette)==GF4_EFORMAT && !host.nevents);
    host.ctx.changing=false;
    REQUIRE(gf4_palette_set(&host.ctx,1,T_ENTRIES,1,palette)==GF4_EINVAL);
    REQUIRE(gf4_palette_set(&host.ctx,1,T_ENTRIES-1,2,palette)==GF4_EINVAL);

    legacy.display4=&host.ctx;
    state.head[0].mode.fmt=GF3_INDEX8;
    state.head[0].enabled=false;
    gf4_host_trace_clear(&host);
    REQUIRE(!gf_display_palette(&legacy,0,T_ENTRIES,palette));
    REQUIRE(state.head[0].lut_dirty && !host.nevents);
    for (u32 i=0;i<T_DATA_WRITES;++i)
        REQUIRE(legacy.palette[i]==palette[i]);
    state.head[0].enabled=true;
    gf4_host_fail_at(&host,D_WRITE,T_FAIL_WRITE,GF2_EIO);
    palette[3]^=1U;
    REQUIRE(gf_display_palette(&legacy,1,1,&palette[3])==GF2_EIO);
    REQUIRE(state.head[0].lut_dirty && host.calls[D_WRITE]==T_FAIL_WRITE);
    for (u32 i=0;i<T_DATA_WRITES;++i)
        REQUIRE(legacy.palette[i]==palette[i] && state.head[0].palette[i]==palette[i]);
    gf4_host_fail_at(&host,D_WRITE,0,0);
    gf4_host_trace_clear(&host);
    host.ctx.changing=true;
    state.next[0]=state.head[0];
    state.next[0].enabled=false;
    palette[6]^=1U;
    REQUIRE(!gf_display_palette(&legacy,2,1,&palette[6]) && !host.nevents);
    REQUIRE(legacy.palette[6]==palette[6] && state.next[0].palette[6]==palette[6]);
    REQUIRE(state.head[0].palette[6]!=palette[6]);
    host.ctx.changing=false;
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
