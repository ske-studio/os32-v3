/* Failure clocks and leave readback, linked against real display code.
 * Independent MMIO oracle: MIT nouveau c03c1a1 dispnv04/nvreg.h:
 * CR31, cursor pos 0x680300 + head*0x2000, PCRTC_START 0x600800,
 * PVIDEO STOP 0x8704/INTR_EN 0x8140/OFFSET 0x8920/LIMIT 0x8908. */
#include "gf_startlog_host.h"
#define _start unused_save_start
#include "gf4_c2save_host.c"
#undef _start
#include "../../drivers/video/geforce/leave_log.h"
static void check_leave(void)
{
    setup();
    host.cr[0][0x31]=0x15; host.cr[1][0x31]=0x24;
    static const u32 offsets[]={0x680300,0x682300,0x600800,0x602800,
        0x8704,0x8140,0x8920,0x8924,0x8908,0x890c};
    for (u32 i=0;i<10;i++) gf3_host_set(&host.base,0,offsets[i],0x100U+i);
    struct gf_io *io=&host.base.f.io;
    startlog_clear();
    geforce_leave_log(io,-77);
    assert(gf2_host_strstr(startlog,"GF4 STATE stage=leave98 route_rc=-77 read_rc=0 valid=00000fff h0_cr31=15 h1_cr31=24"));
    assert(gf2_host_strstr(startlog,"h0_cursor=00000100 h1_cursor=00000101 h0_start=00000102 h1_start=00000103 pvideo_stop=00000104 pvideo_intr_en=00000105 offset0=00000106 offset1=00000107 limit0=00000108 limit1=00000109\n"));
    assert(host.cr[0][0x31]==0x15 && host.cr[1][0x31]==0x24);
    for (u32 i=0;i<host.nevents;i++) if (events[i].op==D_WRITE) {
        assert(events[i].a[2]==1);
        assert(events[i].a[1]==0x6013d4 || events[i].a[1]==0x6033d4);
    }
    gf4_host_trace_clear(&host); startlog_clear();
    gf4_host_fail_at(&host,D_READ,1,-88);
    geforce_leave_log(io,0);
    assert(gf2_host_strstr(startlog,"route_rc=0 read_rc=-88 valid=00000ffe"));
    assert(gf2_host_strstr(startlog,"limit1=00000109"));
}
void _start(void)
{
    setup(); host.local_now=T_BUDGET;
    startlog_clear();
    assert(gf4_display_save(&host.ctx,&saved,T_BUDGET)==GF4_ETIME);
    assert(startlog_equal("GF4 ERROR stage=restore_drain_deadline rc=-409 head=2 remain_us=0 reg=00000000 val=00000000\n"));
    setup();
    routes[1].type=GF4_TMDS;
    host.local_now=T_BUDGET+3U;
    startlog_clear();
    assert(gf4_tmds_dpms(&host.ctx,&routes[1],GF4_OFF,T_BUDGET)==GF4_ETIME);
    assert(startlog_equal("GF4 ERROR stage=tmds_dpms_deadline rc=-409 head=1 remain_us=-3 reg=00000000 val=00000000\n"));
    check_leave();
    gf2_host_exit(0);
}
