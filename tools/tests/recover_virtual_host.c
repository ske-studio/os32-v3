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
static u32 cell=0x1234, writes, peek_calls, begins, ends, route;
static int cui;
static char trace[32768]; static u32 used;
u32 recover_test_irq_save(void) {return 0;}
void recover_test_irq_restore(u32 x) {(void)x;}
void recover_test_outp(u32 p,u32 v) {(void)p;(void)v;die(__LINE__);}
u32 recover_test_inp(u32 p) {CHECK(p==RECOVER_ROUTE_PORT);return route;}
const struct pit_setup *pit_get_setup(void) {return 0;}
static int display_recover_ready(void) {return cui;}
static void display_restore_98(void) {}
static int display_recover_cell_read(u32 *v) {*v=cell;return 0;}
static int display_recover_cell_write(u32 v) {cell=v;writes++;return 0;}
static void console_hw_cursor_enable(void) {}
static int serial_gate_active(void) {return 0;}
static int serial_trygetchar(void) {return -1;}
static int serial_checked_ready(void) {return 1;}
static int serial_checked_put(u32 b) {CHECK(used+1<sizeof(trace));trace[used++]=b;trace[used]=0;return 0;}
static int serial_checked_begin(void) {begins++;return 0;}
static int serial_checked_empty(void) {return 1;}
static void serial_checked_end(void) {ends++;}
struct serial_setup {u32 actual;};
static const struct serial_setup *serial_get_setup(void) {static const struct serial_setup s={9600};return &s;}
static int fake_wait(void *p,u32 us) {(void)p;(void)us;return 0;}
struct hwdev_ops {
    int (*call)(u32,void *,u32,const struct recover_ops *,const struct recover_transport *,const struct recover_profile *);
    void (*release_work)(void);
};
static const struct hwdev_ops *hwdev_module_ops(void) {return 0;}
static int kpeek_call(u32 op,void *p,u32 sz) {(void)op;(void)p;(void)sz;peek_calls++;return 0;}
#define GF_OP_INSPECT 5U
#define GF_OP_ABORT 10U
#define recover_hal_wait_frame fake_wait
#include "virtual_dispatch.inc"
#undef recover_hal_wait_frame
#define SHELL_H
#define ATTR_RED 1
#define ATTR_WHITE 2
static int str_eq(const char *a,const char *b) {while(*a && *a==*b){a++;b++;}return *a==*b;}
static void print(int attr,const char *fmt,...) {(void)attr;(void)fmt;}
static struct api {u32 version;void (*kprintf)(int,const char *,...);i32 (*kdebug_call)(u32,void *,u32);}
    api={78,print,recovery_call}, *g_api=&api;
#include "../../userland/shell/cmd_recover.c"
static bool contains(const char *needle)
{
    for(u32 i=0;i<used;i++){u32 j=0;while(needle[j] && trace[i+j]==needle[j])j++;if(!needle[j])return true;}
    return false;
}
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
    u32 seq=r->seq, crc=r->token.snapshot_crc, spent=r->spent;
    KdebugRecover q={0};
    CHECK(recovery_call(KDEBUG_OP_RECOVER_STATUS,&q,sizeof(q))==-1);
    CHECK(recovery_call(0,0,0)==-1 && !peek_calls);
    CHECK(shell_cmd_recover(4,begin)==1);
    KdebugRecoverVirtual v={.version=RECOVER_VIRTUAL_VERSION,.action=RECOVER_VIRTUAL_STEP,.session=2};
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v))==-1);
    CHECK(r->seq==seq && r->token.snapshot_crc==crc);
    tick_count+=10000;
    CHECK(recover_virtual_call(&v,sizeof(v),PIT_HZ)==-1);
    v.session=1;
    CHECK(recovery_call(KDEBUG_OP_RECOVER_VIRTUAL,&v,sizeof(v))==-1);
    CHECK(!recover_virtual_call(&v,sizeof(v),PIT_HZ));
    CHECK(r->state==REC_PAUSED && r->spent==spent && r->expected_node==RECOVER_NONE);
    CHECK(!r->entered && r->expected_node==RECOVER_NONE && r->seq==6 && !writes);
    v.action=RECOVER_VIRTUAL_END;
    CHECK(!recover_virtual_call(&v,sizeof(v),PIT_HZ));
    CHECK(!borrowed && r->transport==&recovery_transport && r->ops==&recovery_ops);
    CHECK(r->state==REC_VERIFIED && r->s0_verified && r->s1_verified && writes==2 && cell==0x1234);
    CHECK(contains("result=OK target=S0 verified=1 unknown=0"));
    CHECK(contains(" trial=0 lost_lines=0"));
    CHECK(contains("phase=state event=RESTORING"));
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
    output("virtual normal/profile/plan/S0/detach PASS\n");
}
void _start(void) {run();__asm__ volatile("int $0x80"::"a"(1),"b"(0));__builtin_unreachable();}
