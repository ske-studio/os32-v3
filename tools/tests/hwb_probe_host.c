/* Real parser, executor, shell file sink and stage B engine, fake effects only. */
#define _start old_shell_tests
#include "hwb_shell_host.c"
#undef _start
int hwb_engine_call(struct probe_wire *,u32);
void hwb_engine_reset(void);
void hwb_engine_hit(void);
u32 hwb_engine_used(void);
u32 hwb_engine_prints(void);
void hwb_engine_epoch(void);
void hwb_engine_long_name(void);
static u32 malformed, fail_call, probe_calls;
static i32 probe_dispatch(u32 op, void *data, u32 bytes)
{
    if (op != PROBE_OP) return fake_kdebug(op,data,bytes);
    CHECK(bytes==PROBE_WIRE_BYTES);
    probe_calls++;
    if (fail_call) { ((struct probe_wire *)data)->more=1; return OS32_ERR_IO; }
    int rc=hwb_engine_call(data,bytes);
    if (malformed) ((struct probe_wire *)data)->size--;
    return rc;
}
static void probe_setup(void)
{
    setup(); hwb_engine_reset(); api.kdebug_call=probe_dispatch;
    malformed=fail_call=probe_calls=0;
}
static u32 probe_length(const char *s) { u32 n=0; while(s[n]) n++; return n; }
static int contains(const char *text,const char *part)
{
    for (;*text;text++) { u32 n=0; while(part[n] && text[n]==part[n]) n++; if(!part[n]) return 1; }
    return 0;
}
static void configure(void)
{
    CHECK(command("probe","add point.0 ioread8 port 0x100",0)==0);
    CHECK(command("probe","arm",0)==0);
}
void _start(void)
{
    HWB_TEST("probe-syntax"); probe_setup();
    static struct hwb_program pg;
    const char *valid[]={"probe reset","probe list","probe arm serial","probe stop","probe drain",
      "probe add abcdefghijklmnopqrstuvwxyz12345 ioread32 port 0x100",
      "probe add point.0 mmioread16 phys 0x12345000",
      "probe add point.0 idxio8 0x20 0x21 0x1f",
      "probe add point.0 idxmmio8 0x12345020 0x12345021 31"};
    for(u32 n=0;n<sizeof(valid)/sizeof(*valid);n++) { u32 line; CHECK(!hwb_parse(&pg,valid[n],probe_length(valid[n]),&line)); }
    const char *bad[]={"probe add abcdefghijklmnopqrstuvwxyz123456 ioread8 port 1",
      "probe add Point.0 ioread8 port 1","probe arm other","probe add p ioread8 bar 0 1",
      "probe add p ioread8 port 4294967296","probe add p idxio8 1 2"};
    for(u32 n=0;n<sizeof(bad)/sizeof(*bad);n++) { u32 line; CHECK(hwb_parse(&pg,bad[n],probe_length(bad[n]),&line)==HWB_ARG); }
    char padded[HWB_LINE_BYTES+1];
    for(u32 n=0;n<sizeof(padded);n++) padded[n]=' ';
    const char *shortline="probe stop";
    for(u32 n=0;shortline[n];n++) padded[n]=shortline[n];
    u32 line; CHECK(!hwb_parse(&pg,padded,HWB_LINE_BYTES,&line));
    CHECK(hwb_parse(&pg,padded,sizeof(padded),&line)==HWB_ARG);
    CHECK(!command("probe","list",0));
    HWB_TEST("probe-drain"); configure();
    for(u32 n=0;n<20;n++) hwb_engine_hit();
    CHECK(hwb_engine_used()==20);
    CHECK(command("probe","drain",0)==HWB_EXIT_IO && hwb_engine_used()==20);
    add("drain","probe stop\nprobe drain\nend\n"); log_short=7;
    CHECK(!logged("run","drain",0) && !hwb_engine_used());
    CHECK(contains(log_file,"name=point.0 hit=20 item=0"));
    CHECK(contains(log_file,"captured=20 dropped=0 overflow=0 rc=OK"));
    CHECK(log_calls>1 && log_closes==1);
    CHECK(!command("probe","drain",0) && !hwb_engine_used());
    HWB_TEST("probe-output-failure"); probe_setup(); configure();
    for(u32 n=0;n<64;n++) hwb_engine_hit();
    add("drain","probe stop\nprobe drain\nend\n"); log_fail=1;
    CHECK(logged("run","drain",0)==HWB_EXIT_IO && !hwb_engine_used());
    CHECK(terminal_log_rc==OS32_ERR_IO && log_closes==1);
    HWB_TEST("probe-wire"); probe_setup(); malformed=1;
    CHECK(command("probe","stop",0)==HWB_EXIT_IO);
    malformed=0; fail_call=1; u32 before=probe_calls;
    CHECK(command("probe","list",0)==HWB_EXIT_IO && probe_calls==before+1);
    HWB_TEST("probe-epoch"); probe_setup(); configure();
    CHECK(!command("probe","stop",0)); hwb_engine_epoch(); hwbf_clear_trace(&bus);
    CHECK(!command("probe","add point.0 ioread8 port 0x100",0));
    CHECK(contains(bus.lines[1],"action=7") && contains(bus.lines[1],"rc=ARG"));
    HWB_TEST("probe-serial"); probe_setup();
    CHECK(!command("probe","add point.0 ioread8 port 0x100",0));
    CHECK(!command("probe","arm serial",0));
    u32 count=hwb_engine_prints(); hwb_engine_hit();
    CHECK(hwb_engine_prints()==count+1 && hwb_engine_used()==1);
    CHECK(!command("probe","stop",0)); CHECK(!command("probe","drain",0));
    HWB_TEST("probe-long-name"); probe_setup(); hwb_engine_long_name();
    CHECK(!command("probe","add abcdefghijklmnopqrstuvwxyz12345 ioread8 port 0x100",0));
    CHECK(!command("probe","arm",0)); hwb_engine_hit();
    CHECK(!command("probe","stop",0)); hwbf_clear_trace(&bus);
    CHECK(!command("probe","drain",0));
    CHECK(contains(bus.lines[0],"name=abcdefghijklmnopqrstuvwxyz12345 hit=1"));
    HWB_TEST("probe-step-abort"); probe_setup();
    add("step","probe add point.0 ioread8 port 0x100\nprobe arm\nprobe reset\n");
    CHECK(!logged("load","step",0)); CHECK(!command("step",0,0)); CHECK(!command("step",0,0));
    CHECK(command("abort",0,0)==130); hwb_engine_hit(); CHECK(hwb_engine_used()==1);
    CHECK(!command("probe","stop",0)); CHECK(!command("probe","reset",0));
    hwb_test_write("GREEN stage C probe integration\n"); hwb_test_exit(0);
}
