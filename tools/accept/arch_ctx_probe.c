/* W4e test-image observer, included only by substituted shell_boot.c.
 * The real trap, DEC, MPIC, exec and syscall providers remain linked. */
#include "ppc_trap.h"
#include "ppc_hpt.h"
#include "exec_arch.h"
#include "exec.h"
#include "kernel/irq/irq.h"
#include "kernel_tick.h"
#include "mpic.h"
#include "platform_kboot_layout.h"
#include "kprintf.h"
#include "kstring.h"
#include "con_sink.h"
#include "sys.h"
#include "paging.h"
#include "arch_exec_kapi.h"
#include "kapi_table.h"
#include "exec/appslot.h"
#include "exec/shell_service.h"
#include "fs/fd_redirect.h"
#include "memmap.h"

volatile u32 ctx_started;
volatile u32 ctx_exit_request, ctx_exit_phase, ctx_exit_delivered;
static volatile u32 decs, ipis;
static struct ppc_frame *sentinel;
static struct ppc_frame saved;
/* PPC has no GeForce provider; expose sys_halt rather than its absent KAPI slot. */
void geforce_foreground_poll(void) { }
static int polled;
static u32 exit_code;
static u32 start_tick, expected_gen, reached_gen, fault_gen, kernel_root;
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
    struct ppc_frame *user = call->native;
    check((user->srr1 & PPC_MSR_PR) && ppc_root_current() != kernel_root &&
          exec_nest_level == 2 && res_owner_get() == 2);
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
    check(tick_count > start_tick && ppc_root_current() == kernel_root);
}
void ctx_fault(struct ppc_frame *f)
{
    if (!expected_gen) return;
    check(!kctx_irq_depth && !(f->srr1 & PPC_MSR_PR) &&
          reached_gen == expected_gen && f->srr0 >= MEM_SHELL_LOAD_ADDR &&
          f->srr0 < MEM_SHELL_HEAP_BASE && f->busy && *f->busy == 1);
    check((expected_gen == 2 && f->vector == PPC_VECTOR_DSI && f->dar == MEM_SHELL_GUARD) ||
          (expected_gen == 3 && f->vector == 0x700 && (f->srr1 & 0x20000)));
    fault_gen = expected_gen;
}
static u32 resident(unsigned gen, unsigned argc, char **argv, const u32 *api,
                     ShellService *aux, unsigned sp, int (*fp)(int), u32 lease_id)
{
    check(gen == expected_gen && argc == 2 && argv && !kstrcmp(argv[1], "probe") && api == exec_kapi_table() &&
          aux == &g_shell_service && aux->generation == gen);
    check(sp > MEM_SHELL_GUARD && sp < MEM_SHELL_STACK_TOP && !(sp & 15) &&
          !(msr() & PPC_MSR_PR) && (msr() & PPC_MSR_FP) &&
          ppc_root_current() == kernel_root && exec_nest_level == 1 && res_owner_get() == 1 && !appslot_get(APP_ID_SHELL)->as);
    static u32 lease;
    u32 old = lease;
    lease = lease_id;
    reached_gen = gen;
    kprintf(7, "CTX entry gen=%u pr=0 root=%x sp=%x argc=%u argv=%x api=%x aux=%x\n",
            gen, kernel_root, sp, argc, (u32)argv, (u32)api, (u32)aux);
    if (gen != 1) return old;
    check(fp(1));
    (void)exec_run("/bin/ctx.bin");
    check((msr() & PPC_MSR_FP) && fp(0));
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
    check(fp(1));
    (void)exec_run("/bin/ctx.bin fault");
    check((msr() & PPC_MSR_FP) && fp(0));
    check(!exec_last_result(&kind, &code) && kind == EXEC_KIND_FAULT);
    state();
    kprintf(7, "CTX lifecycle normal=1 exit=1 kill=1 park=1 resume=1 tick=%u\n",
            (u32)(tick_count-start_tick));
    check(ppc_root_current() == kernel_root && (msr() & PPC_MSR_FP));
    return old;
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
    ((KernelAPI *)exec_kapi_table())->kapi_reserved[0] = (void *)resident;
    kernel_root = ppc_root_current();
    ctx_started = 1;
    exec_nest_level = 0; /* Fixture boot starts the first actual resident image. */
#if CTX_OUTSIDE == 2
    kprintf(7, "CTX outside 300\n");
    *(volatile u32 *)MEM_SHELL_GUARD = 1;
#elif CTX_OUTSIDE == 3
    kprintf(7, "CTX outside 700\n");
    __asm__ volatile("trap");
#endif
    for (unsigned gen = 1; gen <= 4; ++gen) {
        char cmd[] = "/bin/r1.bin probe";
        cmd[6] = '0' + gen;
        unsigned fp = msr() & PPC_MSR_FP;
        expected_gen = gen;
        (void)exec_run(cmd);
        int kind, code;
        check(!exec_last_result(&kind, &code) && reached_gen == gen &&
              kind == (gen == 2 || gen == 3 ? EXEC_KIND_FAULT : EXEC_KIND_EXITED));
        if (gen == 2 || gen == 3) check(fault_gen == gen);
        if (gen == 4) check(code == 17);
        check(exec_nest_level == 0 && !res_owner_get() && !(fp ^ (msr() & PPC_MSR_FP)));
        check(g_shell_service.font_source_validate(0) < 0 &&
              shell_service_reject_last == SHELL_SVC_REJ_INACTIVE);
        u32 tick = tick_count;
        while (tick_count == tick) { }
        kprintf(7, "CTX retired gen=%u kind=%d tick=%u\n", gen, kind, tick_count);
    }
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
