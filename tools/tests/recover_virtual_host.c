/* Real dispatcher and CLI bodies, fake HAL/serial endpoints; ILP32/qemu. */
#include "../../kdebug/recover.c"
#include "../../kdebug/recover_profile.c"
#include "../../kdebug/recover_virtual.c"
static void output(const char *s)
{
    u32 n=0; while(s[n]) n++;
    __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory");
}
static void die(u32 line)
{
    char b[12]; u32 i=11; b[i]=0;
    do {b[--i]='0'+line%10;line/=10;}while(line);
    output("FAIL line ");output(b+i);output("\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(1));
    __builtin_unreachable();
}
#define CHECK(x) do {if(!(x)) die(__LINE__);}while(0)
volatile u32 tick_count;
static bool advance_ticks, poll_test, restore_clock_checked;
static u32 frozen_samples;
static bool tx_test, uart_owned;
static u32 busy_ready, busy_waits, polls_before_busy, busy_bytes, bytes_after_busy;
static u32 tick_reads;
static u32 fake_ticks(void)
{
    if (advance_ticks) {
        CHECK(++tick_reads < 100000U);
        CHECK(tick_count < 10U * PIT_HZ); /* finite watchdog, before 60s opcode limit */
        if (recover_owner()->state==REC_RUNNING && !recover_owner()->emitting &&
            tick_reads % 4U == 0) tick_count++;
    }
    return tick_count;
}
static u32 cell=0x1234, writes, peek_calls, begins, ends, route, setup_reads, gf_calls;
static int cui;
static char trace[32768]; static u32 used;
u32 recover_test_irq_save(void) {return 0;}
void recover_test_irq_restore(u32 x) {(void)x;}
void recover_test_outp(u32 p,u32 v) {(void)p;(void)v;die(__LINE__);}
u32 recover_test_inp(u32 p) {CHECK(p==RECOVER_ROUTE_PORT);return route;}
const struct pit_setup *pit_get_setup(void) {return 0;}
static int display_recover_ready(void) {return cui;}
static void display_restore_98(void)
{
    if (poll_test) {
        struct recover *r=recover_owner();
        tick_count+=7;
        CHECK(r->ops->ticks(r->opaque)-r->restore_tick==7);
    }
}
static int display_recover_cell_read(u32 *v) {*v=cell;return 0;}
static int display_recover_cell_write(u32 v) {cell=v;writes++;return 0;}
static void console_hw_cursor_enable(void) {}
static int serial_gate_active(void) {return 0;}
static int serial_trygetchar(void)
{
    if (poll_test && clock_frozen) {
        struct recover *r=recover_owner();
        CHECK(++frozen_samples<=128U); /* bounded even with the poll comparison removed */
        tick_count++;
        CHECK(r->ops->ticks(r->opaque)==freeze_tick);
    }
    return -1;
}
static int counted_virtual_ready(void *opaque)
{
    if (tx_busy) CHECK(++busy_ready<=4096U);
    return virtual_tx_ready(opaque);
}
static int serial_checked_ready(void) {return 1;}
static int serial_checked_put(u32 b) {CHECK(!tx_test || !tx_busy);CHECK(used+1<sizeof(trace));trace[used++]=b;trace[used]=0;return 0;}
static int serial_checked_begin(void)
{
    if (poll_test && recover_owner()->state==REC_RESTORING && !restore_clock_checked) {
        struct recover *r=recover_owner();
        CHECK(r->ops->ticks(r->opaque)==r->restore_tick);
        CHECK(r->restore_tick==freeze_tick);
        /* RESTORING is now emitted before local cleanup thaws the fake clock. */
        if(!clock_frozen)restore_clock_checked=true;
    }
    if (tx_test) {
        CHECK(!uart_owned);
        uart_owned=true;
        if (tx_busy && !busy_waits) {
            polls_before_busy=recover_owner()->uart_polls;
            busy_bytes=used;
            CHECK(clock_frozen);
        } else if (recover_owner()->state==REC_RESTORING && !clock_frozen) {
            CHECK(!tx_busy && !clock_frozen);
        }
    }
    begins++;return 0;
}
static int serial_checked_empty(void) {return 1;}
static void serial_checked_end(void)
{
    if (tx_test) {
        CHECK(uart_owned);
        uart_owned=false;
        if (tx_busy) {
            CHECK(recover_owner()->uart_polls==4096);
            bytes_after_busy=used;
        }
    }
    ends++;
}
struct serial_setup {u32 actual;};
static const struct serial_setup *serial_get_setup(void) {setup_reads++;static const struct serial_setup s={9600};return &s;}
static int fake_wait(void *p,u32 us)
{
    (void)p;(void)us;
    if (tx_test && tx_busy) {
        CHECK(++busy_waits<=4096U); /* finite watchdog when UART limit is removed */
        tick_count++;
        CHECK(clock_frozen && virtual_ticks(0)==freeze_tick);
    }
    return 0;
}
struct hwdev_ops {
    int (*call)(u32,void *,u32,const struct recover_ops *,const struct recover_transport *,const struct recover_profile *);
    void (*release_work)(void);
};
static int gf_abort(u32 op, void *buf, u32 size, const struct recover_ops *base,
                    const struct recover_transport *transport, const struct recover_profile *s0)
{
    (void)base; (void)transport; (void)s0;
    CHECK(op==10 && size==sizeof(u32));
    struct recover *r=recover_owner();
    if (*(u32 *)buf!=r->token.session) return -1;
    gf_calls++;
    return trial_end(r,&r->token,REC_ABORT);
}
static void release_work(void) {}
static const struct hwdev_ops *hwdev_module_ops(void)
{
    static const struct hwdev_ops hw={gf_abort,release_work};
    return &hw;
}
static int kpeek_call(u32 op,void *p,u32 sz) {(void)op;(void)p;(void)sz;peek_calls++;return 0;}
#define GF_OP_INSPECT 5U
#define GF_OP_ABORT 10U
#define ATTR_RED 1
#define ATTR_WHITE 2
static void print(int attr,const char *fmt,...) {(void)attr;(void)fmt;}
#define kprintf print
#define recover_hal_wait_frame fake_wait
#define tick_count fake_ticks()
#include "virtual_dispatch.inc"
#undef tick_count
#undef recover_hal_wait_frame
#undef kprintf
#define SHELL_H
static int str_eq(const char *a,const char *b) {while(*a && *a==*b){a++;b++;}return *a==*b;}
static struct api {u32 version;void (*kprintf)(int,const char *,...);i32 (*kdebug_call)(u32,void *,u32);}
    api={78,print,recovery_call}, *g_api=&api;
#include "../../userland/shell/cmd_recover.c"
static u32 trace_at(const char *needle)
{
    for(u32 i=0;i<used;i++){u32 j=0;while(needle[j] && trace[i+j]==needle[j])j++;if(!needle[j])return i;}
    return used;
}
static bool contains(const char *needle) {return trace_at(needle)<used;}
static void run(void)
{
    struct recover *r=recover_owner();
    char *begin[]={"recover","virtual","begin","normal"};
    CHECK(!shell_cmd_recover(4,begin));
    CHECK(r->state==REC_PAUSED && borrowed && r->token.session==1 && r->plan==&plan);
    CHECK(r->token.profile_crc==recover_profile_crc(&profile));
    u32 value=123;
    struct recover_bus_access access={0};
    CHECK(r->ops->read(r->opaque,&access,&value)==-1 && value==123);
    CHECK(r->ops->write(r->opaque,&access,456)==-1);
    CHECK(!writes && !profile.count && !profile.rule_count && !profile.quiesce_count);
    CHECK(!profile.expected_bdf && !profile.expected_device && !profile.cold_required);
    struct recover_identity id;
    u32 command, bars[3];
    CHECK(!ops.identity(r->opaque,&id) && id.boot==VIRTUAL_ID && !id.device);
    CHECK(!ops.raw_config(r->opaque,&command,bars) && command==RECOVER_COMMAND_MEM);
    CHECK(!bars[0] && !bars[1] && !bars[2]);
    CHECK(transport.frame_us==recovery_transport.frame_us && transport.max_polls==4096);
    CHECK(contains("phase=state event=SNAPSHOTTING"));
    CHECK(contains("phase=state event=ARMED") && contains("phase=state event=RUNNING"));
    CHECK(contains("phase=state event=PAUSED") && !contains("phase=screen"));
    CHECK(r->seq==4);
    CHECK(!ops.route_write && profile.target_count==1);
    u32 seq=r->seq, crc=r->token.snapshot_crc, spent=r->spent, log_bytes=used;
    KdebugRecover q={0};
    CHECK(recovery_call(KDEBUG_OP_RECOVER_STATUS,&q,sizeof(q))==-1);
    CHECK(recovery_call(0,0,0)==-1 && !peek_calls);
    CHECK(shell_cmd_recover(4,begin)==1);
    KdebugRecoverVirtual v={.version=RECOVER_VIRTUAL_VERSION,.action=RECOVER_VIRTUAL_STEP,.session=2};
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v))==-1);
    CHECK(r->seq==seq && r->token.snapshot_crc==crc && used==log_bytes);
    tick_count+=10000;
    v.session=1;
    /* Same session, foreign ops: rejected at the gate, before attach. */
    u32 reads=setup_reads;
    r->ops=&recovery_ops;
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v))==-1);
    CHECK(setup_reads==reads && r->seq==seq);
    r->ops=&ops;
    v.version=0;
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v))==-1);
    v.version=RECOVER_VIRTUAL_VERSION;
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v)-1)==-1);
    v.reserved[0]=1;
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v))==-1);
    v.reserved[0]=0;
    CHECK(r->seq==seq && r->token.snapshot_crc==crc && used==log_bytes);
    char *step[]={"recover","virtual","step","1"};
    CHECK(!shell_cmd_recover(4,step));
    CHECK(r->state==REC_PAUSED && r->spent==spent && r->expected_node==RECOVER_NONE);
    CHECK(!r->entered && r->expected_node==RECOVER_NONE && r->seq==6 && !writes);
    v.action=RECOVER_VIRTUAL_END;
    char *end[]={"recover","virtual","end","1"};
    CHECK(!shell_cmd_recover(4,end));
    CHECK(!borrowed && r->transport==&recovery_transport && r->ops==&recovery_ops);
    CHECK(r->state==REC_VERIFIED && r->s0_verified && r->s1_verified && writes==2 && cell==0x1234);
    CHECK(contains("result=OK target=S0 verified=1 unknown=0"));
    CHECK(contains(" trial=0 lost_lines=0"));
    CHECK(contains("phase=state event=RESTORING"));
    CHECK(contains(" cleanup_seconds="));
    CHECK(contains("phase=diag event=TX fail_seq=0 pos=0 fn=NONE rc=0 where=NONE sent=0 polls=0 bytes=0\n"));
    CHECK(contains(" txd=0\n"));
    CHECK(trace_at("phase=diag event=TX")<trace_at("phase=final event=END"));
    CHECK(begins==ends && !r->lost_lines);
    /* A failed start must give back transport/ops without consuming a session. */
    u32 session=r->token.session;
    cui=-1;
    CHECK(shell_cmd_recover(4,begin)==1);
    CHECK(!borrowed && r->ops==&recovery_ops && r->transport==&recovery_transport);
    cui=0; route=RECOVER_ROUTE_MASK;
    CHECK(shell_cmd_recover(4,begin)==1);
    CHECK(!borrowed && r->token.session==session && r->state==REC_VERIFIED);
    CHECK(r->ops==&recovery_ops && r->transport==&recovery_transport);
    route=0; used=0; trace[0]=0;
    CHECK(!shell_cmd_recover(4,begin));
    q.session=r->token.session;
    CHECK(!recovery_call(KDEBUG_OP_RECOVER_NOW,&q,sizeof(q)));
    CHECK(r->state==REC_VERIFIED && r->reason==REC_ABORT);
    CHECK(contains("result=OK target=S0 verified=1 unknown=0"));
    CHECK(contains(" trial=1 lost_lines=0"));
    CHECK(!borrowed && r->ops==&recovery_ops && r->transport==&recovery_transport);
    /* Post-GF detach; the fake ends the trial with trial_end(ABORT) like gfinit_call. */
    CHECK(!shell_cmd_recover(4,begin));
    u32 sid=r->token.session+1, abort_seq=r->seq;
    CHECK(recovery_call(GF_OP_ABORT,&sid,sizeof(sid))==-1);
    CHECK(borrowed && r->state==REC_PAUSED && r->seq==abort_seq);
    sid=r->token.session;
    CHECK(!recovery_call(GF_OP_ABORT,&sid,sizeof(sid)));
    CHECK(gf_calls==1 && r->state==REC_VERIFIED && r->reason==REC_ABORT);
    CHECK(!borrowed && r->ops==&recovery_ops && r->transport==&recovery_transport);
    CHECK(!shell_cmd_recover(4,begin));
    q.session=r->token.session;
    CHECK(!recovery_call(KDEBUG_OP_RECOVER_NOW,&q,sizeof(q)));
    CHECK(begins==ends);
    CHECK(!contains("phase=inject"));
    used=0; trace[0]=0;
    char *fault[]={"recover","virtual","begin","fault"};
    CHECK(!shell_cmd_recover(4,fault));
    CHECK(r->state==REC_PAUSED && !contains("phase=inject"));
    u32 before_writes=writes;
    v=(KdebugRecoverVirtual){.version=RECOVER_VIRTUAL_VERSION,
        .action=RECOVER_VIRTUAL_STEP,.session=r->token.session};
    /* STEP's zero case_id must not replace the case saved by BEGIN. */
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v))==-1);
    CHECK(r->state==REC_FAILED && r->failed && r->reason==REC_OPERATION);
    CHECK(r->s0_verified && !r->s1_verified && r->lost_lines==0);
    CHECK(contains("phase=inject event=BEGIN step=1 addr=0\n"));
    CHECK(trace_at("phase=inject")<trace_at("phase=state event=RESTORING"));
    CHECK(trace_at("phase=state event=RESTORING")<trace_at("phase=final event=END"));
    CHECK(contains("result=FAIL target=S0 verified=1 unknown=1"));
    CHECK(contains(" trial=3 lost_lines=0"));
    CHECK(writes==before_writes+2 && cell==0x1234 && begins==ends);
    CHECK(!borrowed && r->ops==&recovery_ops && r->transport==&recovery_transport);
    u32 failed_seq=r->seq, failed_used=used;
    CHECK(shell_cmd_recover(4,begin)==1 && shell_cmd_recover(4,fault)==1);
    CHECK(r->state==REC_FAILED && r->reason==REC_OPERATION);
    CHECK(r->seq==failed_seq && used==failed_used);
    output("virtual normal/PAUSED/now/GF-abort/fault/latch PASS\n");
}
static void run_expire(void)
{
    struct recover *r=recover_owner();
    char *begin[]={"recover","virtual","begin","trial-expire"};
    CHECK(!shell_cmd_recover(4,begin));
    CHECK(r->state==REC_PAUSED && !contains("phase=inject"));
    CHECK(profile.trial_limit==8U*PIT_HZ && profile.opcode_limit==60U*PIT_HZ);
    CHECK(profile.restore_limit==10U*PIT_HZ && profile.max_polls==65536U);
    CHECK(profile.max_steps==4096U && profile.restore_log_bytes==4096U);
    CHECK(profile.crc==recover_profile_crc(&profile) && r->token.profile_crc==profile.crc);
    u32 crc=profile.crc;
    KdebugRecoverVirtual v={.version=RECOVER_VIRTUAL_VERSION,
        .action=RECOVER_VIRTUAL_STEP,.session=r->token.session};
    advance_ticks=true;
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v))==-1);
    advance_ticks=false;
    CHECK(r->state==REC_FAILED && r->reason==REC_TIMEOUT);
    CHECK(r->spent>=8U*PIT_HZ && r->spent<10U*PIT_HZ);
    CHECK(r->polls>1 && r->polls<profile.max_polls);
    CHECK(r->spent-r->opcode_spent<profile.opcode_limit);
    CHECK(profile.crc==crc && profile.crc==recover_profile_crc(&profile));
    CHECK(contains("phase=inject event=BEGIN step=2 addr=0\n"));
    CHECK(trace_at("phase=inject")<trace_at("phase=state event=RESTORING"));
    CHECK(contains("result=FAIL target=S0 verified=1 unknown=1"));
    CHECK(contains(" trial=2 lost_lines=0"));
    CHECK(r->s0_verified && !r->s1_verified && !r->lost_lines);
    CHECK(!borrowed && r->ops==&recovery_ops && r->transport==&recovery_transport);
    CHECK(writes==2 && cell==0x1234 && begins==ends);
    CHECK(shell_cmd_recover(4,begin)==1 && r->state==REC_FAILED);
    output("virtual trial-expire/8s/CRC/polls/watchdog PASS\n");
}
static void run_poll_stop(void)
{
    struct recover *r=recover_owner();
    char *begin[]={"recover","virtual","begin","poll-stop"};
    CHECK(!shell_cmd_recover(4,begin));
    CHECK(profile.max_polls==64 && profile.trial_limit==60U*PIT_HZ);
    CHECK(profile.opcode_limit==60U*PIT_HZ && !clock_frozen);
    CHECK(profile.crc==recover_profile_crc(&profile));
    u32 crc=profile.crc;
    poll_test=true;
    KdebugRecoverVirtual v={.version=RECOVER_VIRTUAL_VERSION,
        .action=RECOVER_VIRTUAL_STEP,.session=r->token.session};
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v))==-1);
    poll_test=false;
    CHECK(r->state==REC_FAILED && r->reason==REC_TIMEOUT && r->polls==65);
    CHECK(frozen_samples>0 && clock_offset==frozen_samples);
    CHECK(!clock_frozen && restore_clock_checked && tick_count==frozen_samples+7);
    CHECK(r->spent==0 && virtual_ticks(r->opaque)-r->restore_tick==7);
    CHECK(profile.crc==crc && profile.crc==recover_profile_crc(&profile));
    CHECK(contains("phase=inject event=BEGIN step=3 addr=0\n"));
    CHECK(trace_at("phase=inject")<trace_at("phase=state event=RESTORING"));
    CHECK(contains("result=FAIL target=S0 verified=1 unknown=1"));
    CHECK(contains(" trial=2 lost_lines=0"));
    CHECK(r->s0_verified && !r->s1_verified && !r->lost_lines);
    CHECK(!borrowed && r->ops==&recovery_ops && r->transport==&recovery_transport);
    CHECK(writes==2 && cell==0x1234 && begins==ends);
    output("virtual poll-stop/65-polls/frozen-delta-0/restore-clock PASS\n");
}
static void run_txbusy(void)
{
    struct recover *r=recover_owner();
    char *begin[]={"recover","virtual","begin","txbusy-stop"};
    CHECK(!shell_cmd_recover(4,begin));
    CHECK(r->state==REC_PAUSED && !contains("phase=inject"));
    CHECK(transport.uart.put==recovery_transport.uart.put);
    transport.uart.ready=counted_virtual_ready;
    tx_test=true;
    KdebugRecoverVirtual v={.version=RECOVER_VIRTUAL_VERSION,
        .action=RECOVER_VIRTUAL_STEP,.session=r->token.session};
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v))==-1);
    tx_test=false;
    CHECK(busy_ready==4096U-polls_before_busy && busy_waits==busy_ready && polls_before_busy>0);
    CHECK(busy_bytes==bytes_after_busy && busy_waits==clock_offset);
    CHECK(!clock_frozen && !tx_busy && !uart_owned && begins==ends);
    CHECK(r->state==REC_FAILED && r->reason==REC_LOG && r->lost_lines==1);
    CHECK(r->s0_verified && !r->s1_verified && writes==2 && cell==0x1234);
    CHECK(contains("phase=inject event=BEGIN step=4 addr=0\n"));
    CHECK(contains("result=FAIL target=S0 verified=1 unknown=1"));
    CHECK(contains(" trial=9 lost_lines=1"));
    CHECK(contains("phase=diag event=TX fail_seq=7 pos=1 fn=BUDGET rc=-31 where=BYTE sent=0"));
    CHECK(contains(" txd=1\n"));
    /* PAUSED (7) is lost; virtual_tx_end clears busy before RESTORING (8),
     * while local cleanup subsequently thaws the virtual clock. */
    u32 last=0, gaps=0;
    for (u32 i=0;i+5<used;i++) {
        if (trace[i]!=' ' || trace[i+1]!='s' || trace[i+2]!='e' ||
            trace[i+3]!='q' || trace[i+4]!='=') continue;
        u32 n=0,j=i+5;
        while (trace[j]>='0' && trace[j]<='9') n=n*10+trace[j++]-'0';
        if (n!=last+1) {CHECK(last==6 && n==8);gaps++;}
        last=n;
    }
    CHECK(gaps==1 && last==r->seq);
    CHECK(!borrowed && r->ops==&recovery_ops && r->transport==&recovery_transport);
    output("virtual txbusy/ready-count/seq-gap/release/S0 PASS\n");
}
void _start(void)
{
    if (TEST_EXPIRE==3) run_txbusy();
    else if (TEST_EXPIRE==2) run_poll_stop();
    else if (TEST_EXPIRE==1) run_expire(); else run();
    __asm__ volatile("int $0x80"::"a"(1),"b"(0));__builtin_unreachable();
}
