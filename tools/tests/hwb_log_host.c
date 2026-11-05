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
    CHECK(hwbf_equal(fake.lines[3], "HW run=3 seq=3 line=8 phase=end op=poll bdf=0:8.0 addr=0x10 val=0x00000005 reads=7 elapsed_us=0x100000001 rc=OK"));
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
    HWB_TEST("unresolved");
    r.op=HWB_READ; r.space=HW_PHYS; r.width=1; r.bdf=U32_MAX; r.rc=HWB_IO;
    hwb_log_begin(&fake.backend,&r); hwb_log_end(&fake.backend,&r);
    CHECK(hwbf_equal(fake.lines[8], "HW run=3 seq=6 line=8 phase=begin op=mmioread8 target=0:8.0/bar1+0x3C4 addr=UNRESOLVED val=0xFFFFFFFF rc=PENDING"));
    CHECK(hwbf_equal(fake.lines[9], "HW run=3 seq=6 line=8 phase=end op=mmioread8 addr=UNRESOLVED val=0xFFFFFFFF rc=IO"));
    r.rc=HWB_OK;
    HWB_TEST("exit");
    CHECK(hwb_exit_status(HWB_OK)==0 && hwb_exit_status(HWB_ARG)==2 && hwb_exit_status(HWB_NOSYS)==3);
    CHECK(hwb_exit_status(HWB_WINDOW)==4 && hwb_exit_status(HWB_IO)==5 && hwb_exit_status(HWB_TIMEOUT)==124 && hwb_exit_status(HWB_ABORT)==130);
    CHECK(hwb_exit_status((enum hwb_rc)99)==5 && hwbf_equal(hwb_rc_name((enum hwb_rc)99),"IO"));
    HWB_TEST("opcodes");
    static const struct { enum hwb_opcode op; const char *begin; const char *end; } ops[] = {
        {HWB_LET, "let", "let"}, {HWB_DEVICE_BDF, "device", "device"},
        {HWB_DEVICE_ID, "device", "device"}, {HWB_WINDOW_BAR, "window", "window"},
        {HWB_LABEL, "label", "label"}, {HWB_GOTO, "goto", "goto"},
        {HWB_IF, "if", "if"}, {HWB_CALL, "call", "call"},
        {HWB_RETURN, "return", "return"}, {HWB_END, "end", "end"}
    };
    hwbf_clear_trace(&fake); r.run=0; r.seq=0; r.line=0;
    for (u32 i=0; i<sizeof(ops)/sizeof(ops[0]); i++) {
        char expected[100]; u32 n=0;
        const char *parts[] = {"HW run=0 seq=0 line=0 phase=begin op=", ops[i].begin, " rc=PENDING"};
        for (u32 j=0; j<3; j++) for (const char *p=parts[j]; *p; p++) expected[n++]=*p;
        expected[n]=0; r.op=ops[i].op; hwb_log_begin(&fake.backend,&r);
        CHECK(hwbf_equal(fake.lines[2*i], expected));
        n=0; parts[0]="HW run=0 seq=0 line=0 phase=end op="; parts[1]=ops[i].end; parts[2]=" rc=OK";
        for (u32 j=0; j<3; j++) for (const char *p=parts[j]; *p; p++) expected[n++]=*p;
        expected[n]=0; hwb_log_end(&fake.backend,&r);
        CHECK(hwbf_equal(fake.lines[2*i+1], expected));
    }
    CHECK(fake.line_count == 2U * (sizeof(ops)/sizeof(ops[0])) && fake.event_count == fake.line_count);
    HWB_TEST("boundary");
    hwbf_clear_trace(&fake); r.op=HWB_LOG;
    char input[HWB_LINE_BYTES], expected[HWB_LOG_BYTES]; u32 n=0;
    const char *start="HW run=0 seq=0 line=0 phase=end op=log text=\"";
    const char *end="\" rc=OK";
    for (const char *p=start; *p; p++) expected[n++]=*p;
    for (u32 i=0; i<HWB_LINE_BYTES; i++) {
        input[i]='\n'; expected[n++]='\\'; expected[n++]='x'; expected[n++]='0'; expected[n++]='A';
    }
    for (const char *p=end; *p; p++) expected[n++]=*p;
    expected[n]=0;
    hwb_log_text(&fake.backend,&r,input,sizeof(input));
    CHECK(fake.line_count == 1 && fake.event_count == 1 && hwbf_equal(fake.lines[0], expected));
    hwb_test_write("PASS hwb log\n"); hwb_test_exit(0);
}
