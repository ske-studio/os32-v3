/* MIT nouveau dispnv04/crtc.c:175-233, hw.c:85-124, hw.h:238-253. */
#define GF4_HOST_UNIT 5
#include "gf4_display_fake.h"
#include "../../hwdev/gf/internal.h"
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
/* crtc.c:198-233: indexed VGA controls and 10 ms settling interval. */
#define T_OWNER 0x44U
#define T_RPC1 0x1aU
#define T_MODE 0x17U
#define T_RESET 0U
#define T_CLOCK 1U
#define T_OFF 0x20U
#define T_ENABLE 0x80U
#define T_HSYNC 0x80U
#define T_VSYNC 0x40U
#define T_DELAY_US 10000U
#define T_OWNER_HEAD1 3U
#define T_RESET_START 1U
#define T_RESET_END 3U
/* Arbitrary distinct initial register values expose unintended changes. */
#define T_INITIAL_CLOCK 0x85U
#define T_INITIAL_MODE 0x35U
#define T_INITIAL_RPC 0x12U
#define T_OTHER_MODE (T_INITIAL_MODE | T_ENABLE)
#define T_HEAD 1U
#define T_OTHER_HEAD 0U
#define T_BUDGET (2U * T_DELAY_US)
static struct gf4_host host;
static struct gf4_state state;
#ifdef GF_PRODUCTION_MODULE
static struct gf_display_context legacy;
int gf_display_blank(struct gf_display_context *,int);
#endif
static struct gf4_host_event events[256];
struct expected_io { enum gf4_host_op op; u32 off, value; };
static int check_sequence(enum gf4_dpms mode)
{
    const u32 cr=GF4_HOST_CR+T_HEAD*GF4_HOST_STRIDE;
    const u32 sr=GF4_HOST_SR+T_HEAD*GF4_HOST_STRIDE;
    const u32 sync[]={0,T_HSYNC,T_VSYNC,T_HSYNC|T_VSYNC};
    const struct expected_io expected[]={
        {D_WRITE,GF4_HOST_CR,T_OWNER}, {D_WRITE,GF4_HOST_CR+1,T_OWNER_HEAD1},
        {D_WRITE,cr,T_RPC1}, {D_READ,cr+1,0},
        {D_WRITE,sr,T_RESET}, {D_WRITE,sr+1,T_RESET_START},
        {D_WRITE,sr,T_CLOCK}, {D_READ,sr+1,0},
        {D_WRITE,sr,T_CLOCK}, {D_WRITE,sr+1,T_INITIAL_CLOCK|(mode==GF4_ON ? 0 : T_OFF)},
        {D_WRITE,cr,T_MODE}, {D_READ,cr+1,0},
        {D_SLEEP,0,T_DELAY_US},
        {D_WRITE,cr,T_MODE}, {D_WRITE,cr+1,T_INITIAL_MODE|(mode==GF4_OFF ? 0 : T_ENABLE)},
        {D_WRITE,sr,T_RESET}, {D_WRITE,sr+1,T_RESET_END},
        {D_WRITE,cr,T_RPC1}, {D_WRITE,cr+1,T_INITIAL_RPC|sync[mode]}
    };
    u32 n=0;
    for (u32 i=0;i<host.nevents;i++) {
        const struct gf4_host_event *e=&host.events[i];
        if (e->op!=D_READ && e->op!=D_WRITE && e->op!=D_SLEEP) continue;
        REQUIRE(n<sizeof(expected)/sizeof(expected[0]));
        REQUIRE(e->op==expected[n].op);
        if (e->op==D_SLEEP) REQUIRE(e->a[0]==expected[n].value);
        else {
            REQUIRE(e->a[0]==GF_MODE_REGISTER_BAR && e->a[1]==expected[n].off && e->a[2]==1);
            if (e->op==D_WRITE) REQUIRE(e->a[3]==expected[n].value);
        }
        n++;
    }
    REQUIRE(n==sizeof(expected)/sizeof(expected[0]));
    return 0;
}
static int test(void)
{
    const u8 sync[]={0,T_HSYNC,T_VSYNC,T_HSYNC|T_VSYNC};
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=sizeof(events)/sizeof(events[0]);
    state.head[T_HEAD].enabled=true;
    state.head[T_HEAD].dpms=GF4_ON;
    state.head[T_HEAD].cursor_visible=state.head[T_HEAD].cursor_valid=true;
    state.next[T_HEAD]=state.head[T_HEAD];
    for (u32 mode=GF4_ON;mode<=GF4_OFF;mode++) {
        gf4_host_trace_clear(&host);
        host.sr[T_HEAD][T_CLOCK]=T_INITIAL_CLOCK;
        host.cr[T_HEAD][T_MODE]=T_INITIAL_MODE;
        host.cr[T_HEAD][T_RPC1]=T_INITIAL_RPC;
        host.cr[T_OTHER_HEAD][T_MODE]=T_OTHER_MODE;
        REQUIRE(!gf4_dpms_set(&host.ctx,T_HEAD,(enum gf4_dpms)mode,(u32)host.local_now+T_BUDGET));
        REQUIRE(!check_sequence((enum gf4_dpms)mode));
        REQUIRE(host.sr[T_HEAD][T_CLOCK]==(u8)(T_INITIAL_CLOCK|(mode==GF4_ON ? 0 : T_OFF)));
        REQUIRE(host.cr[T_HEAD][T_MODE]==(u8)(T_INITIAL_MODE|(mode==GF4_OFF ? 0 : T_ENABLE)));
        REQUIRE(host.cr[T_HEAD][T_RPC1]==(u8)(T_INITIAL_RPC|sync[mode]));
        REQUIRE(host.cr[T_OTHER_HEAD][T_MODE]==T_OTHER_MODE);
        REQUIRE(host.calls[D_CURSOR_APPLY]==(mode==GF4_ON ? 1U : 0U));
        REQUIRE(host.events[0].op==D_CHANGE_BEGIN && host.events[host.nevents-1].op==D_CHANGE_END);
    }
    gf4_host_trace_clear(&host);
    state.next[T_HEAD].dpms=GF4_OFF; host.sr[T_HEAD][T_CLOCK]=T_OFF;
    REQUIRE(!gf4_blank_set(&host.ctx,T_HEAD,false,(u32)host.local_now+T_BUDGET));
    REQUIRE((host.sr[T_HEAD][T_CLOCK]&T_OFF) && !host.calls[D_CURSOR_APPLY]);
    state.next[T_HEAD].dpms=GF4_ON;
    gf4_host_trace_clear(&host);
    REQUIRE(!gf4_blank_set(&host.ctx,T_HEAD,true,(u32)host.local_now+T_BUDGET));
    REQUIRE(state.next[T_HEAD].blank && !host.calls[D_CURSOR_APPLY]);
    gf4_host_trace_clear(&host);
    REQUIRE(!gf4_dpms_set(&host.ctx,T_HEAD,GF4_ON,(u32)host.local_now+T_BUDGET));
    REQUIRE(state.next[T_HEAD].blank && !host.calls[D_CURSOR_APPLY]);
    gf4_host_trace_clear(&host);
    REQUIRE(!gf4_blank_set(&host.ctx,T_HEAD,false,(u32)host.local_now+T_BUDGET));
    REQUIRE(!state.next[T_HEAD].blank && host.calls[D_CURSOR_APPLY]==1);
    gf4_host_trace_clear(&host);
    gf4_host_fail_at(&host,D_CHANGE_BEGIN,1,GF4_EBUSY);
    REQUIRE(gf4_dpms_set(&host.ctx,T_HEAD,GF4_ON,(u32)host.local_now+T_BUDGET)==GF4_EBUSY);
    REQUIRE(!host.calls[D_WRITE] && !host.calls[D_CHANGE_END]);
    gf4_host_fail_at(&host,D_CHANGE_BEGIN,0,0);
    gf4_host_trace_clear(&host);
    gf4_host_fail_at(&host,D_DPMS,1,GF2_EIO);
    REQUIRE(gf4_dpms_set(&host.ctx,T_HEAD,GF4_ON,(u32)host.local_now+T_BUDGET)==GF2_EIO);
    REQUIRE(host.calls[D_CHANGE_END]==1 && !host.calls[D_CURSOR_APPLY]);
    REQUIRE(state.head[T_HEAD].cursor_visible && state.head[T_HEAD].cursor_valid && !state.head[T_HEAD].blank);
    gf4_host_fail_at(&host,D_DPMS,0,0);
    gf4_host_trace_clear(&host);
    REQUIRE(gf4_dpms_set(&host.ctx,T_HEAD,GF4_ON,(u32)host.local_now+T_DELAY_US-1U)==GF4_ETIME);
    REQUIRE(!host.calls[D_SLEEP] && !host.calls[D_DPMS] && host.calls[D_CHANGE_END]==1);
#ifdef GF_PRODUCTION_MODULE
    gf4_host_trace_clear(&host);
    state.head[T_OTHER_HEAD].enabled=true; state.next[T_OTHER_HEAD]=state.head[T_OTHER_HEAD];
    legacy.io=host.base.f.io; legacy.display4=&host.ctx;
    u32 deadline=(u32)host.local_now+GF3_TIMEOUT_US;
    REQUIRE(!gf_display_blank(&legacy,1) && legacy.blank);
    REQUIRE(host.events[1].op==D_CHANGE_BEGIN && host.events[1].a[0]==deadline);
    gf4_host_trace_clear(&host);
    gf4_host_fail_at(&host,D_CLOCK,1,GF2_EIO);
    REQUIRE(gf_display_blank(&legacy,0)==GF2_EIO && legacy.blank);
    REQUIRE(!host.calls[D_CHANGE_BEGIN]);
#endif
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
