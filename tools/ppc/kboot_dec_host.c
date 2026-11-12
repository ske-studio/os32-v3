/* ILP32 product code, only privileged operations are fake. */
#include "ppc_trap.h"
#include "kernel_tick.h"
#include "arch_io.h"
#include "kernel/irq/tick.h"
#include "kernel/irq/irq.h"
volatile u32 kctx_irq_depth;
static int dec, writes, enabled;
static u32 calls, mark;
static void (*handler)(struct ppc_frame *);
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(&mark), "d"(4) : "memory");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    __builtin_unreachable();
}
static void check(int ok, u32 stage) { mark=stage; if (!ok) finish(stage); }
int ppc_irq_enabled(void) { return enabled; }
void ppc_enable(void) {
    enabled=1;
#ifdef DEC_IDLE_TEST
    mark=18; finish(0);
#endif
}
void ppc_disable(void) { enabled=0; }
int ppc_read_dec(void) { return dec; }
void ppc_write_dec(u32 value) { dec=(int)value; ++writes; }
void ppc_trap_set_handler(u32 vector, void (*fn)(struct ppc_frame *))
{ check(vector==0x900,2); handler=fn; }
u32 ppc_irq_save(void) { u32 saved=enabled; enabled=0; return saved; }
void ppc_irq_restore(u32 saved) { enabled=(int)saved; }
static void forbidden_callback(void) { check(0,21); }
static void tick_callback(void)
{
    ++calls; check(calls==tick_count,3);
    check(tick_register(1,forbidden_callback)==TICK_ERR_CTX,19);
    check(kctx_irq_depth==1,22);
}
void _start(void)
{
#ifdef DEC_IDLE_TEST
    _idle();
    mark=18; finish(18);
#endif
    check(tick_register(0,tick_callback)==0,23);
    dec=-99;
    ppc_decrementer_interrupt(0);
    check(dec==PPC_DEC_FAR && !tick_count && !calls,4);
    check(ppc_dec_start(25000000,1000)==-1,5);
    check(ppc_dec_start(25000000,100)==0 && handler==ppc_decrementer_interrupt,6);
    check(dec==250000,7);
    int before=writes;
    dec=-750001;
    handler(0);
    check(writes==before+1 && dec==249999,8);
    check(tick_count==4 && calls==4,9);
    check(kctx_irq_depth==0,20);
    dec=100;
    handler(0);
    check(dec==100 && tick_count==4,10);
    check(ppc_dec_start(33333333,100)==0,11);
    u32 sum=dec;
    for (u32 i=1;i<100;++i) {
        dec=0; handler(0);
        check(dec==333333 || dec==333334,12);
        sum+=(u32)dec;
    }
    check(sum==33333333,13);
    enabled=1; before=writes;
    check(ppc_dec_start(25000000,100)==-1 && writes==before,14);
    enabled=0;
    arch_cpu_halt(); check(!enabled,15);
    enabled=1; arch_cpu_halt(); check(enabled,16);
    check(kctx_irq_depth==0,20);
    mark=17; finish(0);
}
