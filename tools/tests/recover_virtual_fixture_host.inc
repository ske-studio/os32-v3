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
#include "../../kdebug/gfinit_wire.h"
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
