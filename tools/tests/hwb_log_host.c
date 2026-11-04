#include "hwb_fake.h"
#include "userland/shell/hwb_log.c"
static struct hwb_fake fake;
void _start(void)
{
    hwbf_init(&fake);
    struct hwb_log_record r = { .run=3, .seq=1, .line=8, .op=HWB_WRITE,
        .space=HW_PHYS, .width=1, .addr=0x204003c4, .value=9,
        .target="0:8.0/bar1+0x3C4", .rc=HWB_OK };
    HWB_TEST("records");
    hwb_log_begin(&fake.backend, &r);
    CHECK(fake.line_count == 1 && fake.event_count == 1);
    CHECK(hwbf_equal(fake.lines[0], "HW run=3 seq=1 line=8 phase=begin op=mmiowrite8 target=0:8.0/bar1+0x3C4 addr=0x204003C4 val=0x09 rc=PENDING"));
    hwb_log_end(&fake.backend, &r);
    CHECK(hwbf_equal(fake.lines[1], "HW run=3 seq=1 line=8 phase=end op=mmiowrite8 addr=0x204003C4 val=0x09 rc=OK"));
    r.op=HWB_READ; r.space=HW_PCI; r.bdf=HW_BDF(0,8,0); r.width=4; r.addr=16; r.value=0xffffffffUL; r.seq=2;
    hwb_log_end(&fake.backend,&r);
    CHECK(hwbf_equal(fake.lines[2], "HW run=3 seq=2 line=8 phase=end op=pciread32 bdf=0:8.0 addr=0x10 val=0xFFFFFFFF rc=OK"));
    r.op=HWB_POLL; r.seq=3; struct hwb_poll_result p={5,7,1,1};
    hwb_log_poll(&fake.backend,&r,&p);
    CHECK(hwbf_equal(fake.lines[3], "HW run=3 seq=3 line=8 phase=end op=poll val=0x5 reads=7 elapsed_us=0x100000001 rc=OK"));
    r.op=HWB_WAIT; r.seq=4; r.requested_us=10; r.elapsed_hi=0; r.elapsed_lo=11;
    hwb_log_end(&fake.backend,&r);
    CHECK(hwbf_equal(fake.lines[4], "HW run=3 seq=4 line=8 phase=end op=wait requested_us=10 elapsed_us=0xB rc=OK"));
    r.op=HWB_PRINT; r.seq=5;
    hwb_log_print(&fake.backend,&r,"a \"\n\\",5,42);
    CHECK(hwbf_equal(fake.lines[5], "HW run=3 seq=5 line=8 phase=end op=print tag=\"a\\x20\\\"\\x0A\\\\\" val=0x2A rc=OK"));
    r.op=HWB_LOG; r.seq=6; hwb_log_text(&fake.backend,&r,"x\t",2);
    CHECK(hwbf_equal(fake.lines[6], "HW run=3 seq=6 line=8 phase=end op=log text=\"x\\x09\" rc=OK"));
    struct hwb_state s={.backend=&fake.backend,.run=3,.rc=HWB_OK,.lines=10,.writes=1};
    hwb_log_finish(&s);
    CHECK(hwbf_equal(fake.lines[7], "HW run=3 op=end rc=OK lines=10 writes=1"));
    CHECK(fake.line_count == 8 && fake.event_count == 8);
    HWB_TEST("exit");
    CHECK(hwb_exit_status(HWB_OK)==0 && hwb_exit_status(HWB_ARG)==2 && hwb_exit_status(HWB_NOSYS)==3);
    CHECK(hwb_exit_status(HWB_WINDOW)==4 && hwb_exit_status(HWB_IO)==5 && hwb_exit_status(HWB_TIMEOUT)==124 && hwb_exit_status(HWB_ABORT)==130);
    CHECK(hwb_exit_status((enum hwb_rc)99)==5 && hwbf_equal(hwb_rc_name((enum hwb_rc)99),"IO"));
    HWB_TEST("boundary");
    struct logbuf b; b.n=0; for (u32 i=0; i<HWB_LOG_BYTES+10U; i++) ch(&b,'X'); emit(&fake.backend,&b);
    CHECK(b.n == HWB_LOG_BYTES-1U && fake.lines[8][HWB_LOG_BYTES-1U]==0);
    hwb_test_write("PASS hwb log\n"); hwb_test_exit(0);
}
