/* B5 fixture: expected bits from nouveau dispnv04/crtc.c:198-233. */
#define GF4_HOST_UNIT 5
#include "gf4_display_fake.h"
#include "../../hwdev/gf/internal.h"
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
static struct gf4_host host;
static struct gf4_state state;
static struct gf_display_context legacy;
int gf_display_blank(struct gf_display_context *,int);
static struct gf4_host_event events[256];
static int test(void)
{
    static const u8 sync[4]={0,0x80,0x40,0xc0};
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=sizeof(events)/sizeof(events[0]);
    state.head[1].enabled=true;
    state.next[1]=state.head[1];
    for (u32 mode=0;mode<4;mode++) {
        gf4_host_trace_clear(&host);
        host.sr[1][1]=0x85; host.cr[1][0x17]=0x35; host.cr[1][0x1a]=0x12;
        REQUIRE(!gf4_dpms_set(&host.ctx,1,(enum gf4_dpms)mode,(u32)host.local_now+20000));
        REQUIRE(host.sr[1][1]==(u8)(0x85|(mode ? 0x20 : 0)));
        REQUIRE(host.cr[1][0x17]==(u8)(0x35|(mode==GF4_OFF ? 0 : 0x80)));
        REQUIRE(host.cr[1][0x1a]==(u8)(0x12|sync[mode]));
        REQUIRE(host.cr[0][0x44]==3 && host.calls[D_SLEEP]==1);
        REQUIRE(host.events[0].op==D_CHANGE_BEGIN && host.events[host.nevents-1].op==D_CHANGE_END);
    }
    gf4_host_trace_clear(&host);
    state.next[1].dpms=GF4_OFF; host.sr[1][1]=0x20;
    REQUIRE(!gf4_blank_set(&host.ctx,1,false,(u32)host.local_now+20000));
    REQUIRE(host.sr[1][1]&0x20);
    gf4_host_trace_clear(&host);
    gf4_host_fail_at(&host,D_CHANGE_BEGIN,1,GF4_EBUSY);
    REQUIRE(gf4_dpms_set(&host.ctx,1,GF4_ON,(u32)host.local_now+20000)==GF4_EBUSY);
    REQUIRE(!host.calls[D_WRITE] && !host.calls[D_CHANGE_END]);
    gf4_host_fail_at(&host,D_CHANGE_BEGIN,0,0);
    gf4_host_trace_clear(&host);
    gf4_host_fail_at(&host,D_DPMS,1,GF2_EIO);
    REQUIRE(gf4_dpms_set(&host.ctx,1,GF4_ON,(u32)host.local_now+20000)==GF2_EIO);
    REQUIRE(host.calls[D_CHANGE_END]==1 && !host.calls[D_CURSOR_APPLY]);
    gf4_host_fail_at(&host,D_DPMS,0,0);
    gf4_host_trace_clear(&host);
    REQUIRE(gf4_dpms_set(&host.ctx,1,GF4_ON,(u32)host.local_now+9999)==GF4_ETIME);
    REQUIRE(!host.calls[D_SLEEP] && !host.calls[D_DPMS] && host.calls[D_CHANGE_END]==1);
    gf4_host_trace_clear(&host);
    state.head[0].enabled=true; state.next[0]=state.head[0];
    legacy.io=host.base.f.io; legacy.display4=&host.ctx;
    u32 deadline=(u32)host.local_now+GF3_TIMEOUT_US;
    REQUIRE(!gf_display_blank(&legacy,1) && legacy.blank);
    REQUIRE(host.events[1].op==D_CHANGE_BEGIN && host.events[1].a[0]==deadline);
    gf4_host_trace_clear(&host);
    gf4_host_fail_at(&host,D_CLOCK,1,GF2_EIO);
    REQUIRE(gf_display_blank(&legacy,0)==GF2_EIO && legacy.blank);
    REQUIRE(!host.calls[D_CHANGE_BEGIN]);
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
