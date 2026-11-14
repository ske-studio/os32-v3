/* W4e test-image observer, included only by substituted shell_boot.c.
 * The real trap, DEC, MPIC, exec and syscall providers remain linked. */
#include "ppc_trap.h"
#include "exec_arch.h"
#include "exec.h"
#include "kernel/irq/irq.h"
#include "kernel_tick.h"
#include "mpic.h"
#include "kboot_layout.h"
#include "kprintf.h"
#include "kstring.h"
#include "con_sink.h"
#include "sys.h"

volatile u32 ctx_started;
volatile u32 ctx_exit_request, ctx_exit_phase, ctx_exit_delivered;
static volatile u32 decs, ipis;
static struct ppc_frame *sentinel;
static struct ppc_frame saved;
/* PPC has no GeForce provider; expose sys_halt rather than its absent KAPI slot. */
void geforce_foreground_poll(void) { }
static int polled;
static u32 exit_code;
static u32 start_tick;
static u32 msr(void) { u32 v; __asm__ volatile("mfmsr %0" : "=r"(v)); return v; }
static void check(int ok)
{
    if (ok) return;
    kprintf(7, "CTX FAIL phase=%u depth=%u\n", ctx_exit_phase, kctx_irq_depth);
    ppc_disable();
    for (;;) __asm__ volatile("" ::: "memory");
}
static void ipi(struct ppc_frame *f) { (void)f; ++ipis; }
void ctx_irq(struct ppc_frame *f, void (*handler)(struct ppc_frame *))
{
    check(kctx_irq_depth == 1 && !(msr() & PPC_MSR_EE));
    if (sentinel) {
        check(!(f->srr1 & PPC_MSR_PR) && (f->srr1 & PPC_MSR_EE));
        check(!memcmp(sentinel, &saved, sizeof(saved)));
    }
    if (ctx_exit_phase) {
        /* The first possible delivery is after rfi to USER. */
        kprintf(7, "CTX delivery pc=%x msr=%x vector=%x\n", f->srr0, f->srr1, f->vector);
        check((f->srr1 & (PPC_MSR_PR | PPC_MSR_EE)) ==
              (PPC_MSR_PR | PPC_MSR_EE));
        ctx_exit_phase = 0;
        ++ctx_exit_delivered;
    }
    if (f->vector == PPC_VECTOR_DEC) ++decs;
    handler(f);
    check(kctx_irq_depth == 1);
}
void ctx_syscall(struct arch_syscall *call)
{
    check((msr() & PPC_MSR_EE) && !kctx_irq_depth);
    if (arch_syscall_slot(call) == __builtin_offsetof(KernelAPI, sys_exit)/sizeof(u32)-2)
        exit_code = ((struct ppc_frame *)call->native)->gpr[3];
    if (!polled) {
        polled = 1;
        sentinel = call->native;
        kmemcpy(&saved, sentinel, sizeof(saved));
        u32 first = tick_count, before = decs;
        check(pm_mpic_send_self_ipi(0));
        while ((u32)(tick_count - first) < 20) {
            if ((u32)(tick_count - first) >= 10) {
                sys_halt(); (void)exec_sys_yield();
            }
        }
        check(ipis && decs > before + 2 && !kctx_irq_depth);
        check(!memcmp(sentinel, &saved, sizeof(saved)));
        sentinel = 0;
        kprintf(7, "CTX irq pr=0 ee=1 depth=1/0 sentinel=1 dec=%u ipi=%u poll=%u\n",
                decs-before, ipis, (u32)(tick_count-first));
    }
    exec_syscall_dispatch(call);
    check((msr() & PPC_MSR_EE) && !kctx_irq_depth);
}
void ctx_exit_arm(void)
{
    /* Assembly independently enables EE just before its product exit mask,
     * with DEC distant, then pends DEC at the selected boundary. */
    if (ctx_exit_delivered < 3) {
        ctx_exit_request = ctx_exit_delivered + 1;
        ppc_write_dec(PPC_DEC_FAR);
    }
}
static void state(void)
{
    check((msr() & PPC_MSR_EE) && !kctx_irq_depth);
    check(tick_count > start_tick);
}
void pm_shell_boot(void)
{
    /* Paint only unused stack below our current frame. A guard margin keeps
     * this observer's calls off the paint; later deeper frames overwrite it. */
    u32 sp;
    __asm__ volatile("mr %0,1" : "=r"(sp));
    u32 *bottom = (u32 *)__kernel_stack_bottom;
    for (u32 *p = bottom; (u32)p < sp - 512; ++p) *p = 0xa55aa55a;
    start_tick = tick_count;
    pm_mpic_set_ipi_handler(ipi);
    kprintf(7, "CTX BEGIN\n");
    ctx_started = 1;
    (void)exec_run("/bin/ctx.bin");
    int kind, code;
    check(!exec_last_result(&kind, &code));
    kprintf(7, "CTX result kind=%d code=%d\n", kind, code);
    check(kind == EXEC_KIND_EXITED && code == 7);
    state();
    check(ctx_exit_delivered == 3 && !ctx_exit_phase);
    kprintf(7, "CTX exit boundaries=3 restored=1\n");
    con_sink_enable();
    int id = exec_start("/bin/ctx.bin child");
    check(id > 0 && exec_app_state(id) == 4);
    state();
    check(exec_resume(id, 0) == 0 && exit_code == 9);
    state();
    id = exec_start("/bin/ctx.bin child");
    check(id > 0 && exec_app_state(id) == 4 && exec_kill(id) == 0);
    state();
    con_sink_disable();
    (void)exec_run("/bin/fault.bin");
    check(!exec_last_result(&kind, &code) && kind == EXEC_KIND_FAULT);
    state();
    kprintf(7, "CTX lifecycle normal=1 exit=1 kill=1 park=1 resume=1 tick=%u\n",
            (u32)(tick_count-start_tick));
    u32 *p = bottom;
    while (*p == 0xa55aa55a && (u32)p < sp - 512) ++p;
    u32 used = (u32)__kernel_stack_top - (u32)p;
    check(p > bottom && used < PM_KERNEL_STACK_SIZE);
    kprintf(7, "CTX stack used=%u size=%u guard=1\n", used, PM_KERNEL_STACK_SIZE);
    kprintf(7, "CTX PASS\n");
    ppc_disable();
    __asm__ volatile(".globl ctx_done\nctx_done: b ctx_done");
    __builtin_unreachable();
}
