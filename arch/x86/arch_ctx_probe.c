/* Diagnostic snapshot only: included by the substituted syscall_entry.c. */
#include "types.h"
static int ctx_can_start(u32 state, int depth, int sink)
{
    return state == 0 && depth == 1 && !sink;
}
static int ctx_valid(u32 flags, u32 cs, u32 depth, int same, int irq)
{
    return !(cs & 3) && !!(flags & 0x200) == !irq &&
           depth == (u32)irq && same;
}
#ifndef CTX_HOST
#include "exec.h"
#include "kernel/irq/irq.h"
#include "kernel_tick.h"
#include "kstack_hw.h"
#include "memmap.h"
#include "paging.h"
#include "kstring.h"
#include "con_sink.h"
#include "sys.h"
#include "platform/pc98/tick_order.h"
extern volatile int exec_exit_status;
extern volatile int exec_nest_level;
volatile u32 ctx_record[20] = {0x43545838};
volatile u32 ctx_boundary[3] = {~0U, ~0U, ~0U};
static u32 *ctx_frame, ctx_saved[13];
static volatile u32 *ctx_marker;
static u32 ctx_start, ctx_spin;
static u32 ctx_flags(void)
{
    u32 f; __asm__ volatile("pushfl; popl %0" : "=r"(f)); return f;
}
static u32 ctx_cs(void)
{
    u32 cs; __asm__ volatile("mov %%cs,%0" : "=r"(cs)); return cs;
}
static void ctx_check(int ok)
{
    if (ok) return;
    ctx_record[1] = 3;
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}
static void ctx_state(void)
{
    ctx_check(ctx_valid(ctx_flags(), ctx_cs(), kctx_irq_depth, 1, 0));
}
void ctx_irq(u32 *f, u32 irq)
{
    if (ctx_record[1] != 1) return;
    ctx_check(ctx_valid(ctx_flags(), ctx_cs(), kctx_irq_depth,
              !ctx_frame || !memcmp(ctx_frame, ctx_saved, sizeof(ctx_saved)), 1));
    if (ctx_frame) {
        ctx_check(!(f[9] & 3) && (f[10] & 0x200));
        ++ctx_record[2];
        ++ctx_record[irq ? 4 : 3];
    }
    /* The USER loop makes no syscall until this timer-side release. */
    if (ctx_marker && !ctx_marker[6] && (f[9] & 3) == 3) {
        if (!ctx_spin) { ctx_spin = tick_count; ctx_record[18] = ctx_record[17]; }
        ctx_check(ctx_record[17] == ctx_record[18]);
        ctx_record[15] = tick_count - ctx_spin;
        if (ctx_record[15] >= 3000) ctx_marker[6] = 1;
    }
}
void ctx_tick_slot(unsigned slot)
{
    if (slot == PC98_TICK_NE2K && ctx_marker && ctx_spin && !ctx_marker[6]) ++ctx_record[16];
}
void ctx_foreground(void)
{
    if (ctx_record[1] != 1) return;
    ctx_state(); ++ctx_record[17];
}
void ctx_irq_leave(void)
{
    if (ctx_record[1] == 1) ctx_check(!kctx_irq_depth && !(ctx_flags() & 0x200));
}
void ctx_syscall(struct arch_syscall *call)
{
    u32 *f = call->native, slot = f[7];
    if (ctx_record[1] != 1) { exec_syscall_dispatch(call); return; }
    ctx_state();
    if (!ctx_record[5]) {
        ctx_frame = f;
        kmemcpy(ctx_saved, f, sizeof(ctx_saved));
        u32 first = tick_count;
        while ((u32)(tick_count - first) < 20 || !ctx_record[4]) {
            ctx_check((u32)(tick_count - first) < 6000); sys_halt();
        }
        ctx_state();
        ctx_check(ctx_record[2] > 2 && ctx_record[4] && !memcmp(f, ctx_saved, sizeof(ctx_saved)));
        ctx_record[5] = tick_count - first;
        ctx_frame = 0;
    }
    if (slot == KAPI_SLOT_SYS_EXIT) ctx_marker = 0;
    exec_syscall_dispatch(call);
    if (slot == KAPI_SLOT_SYS_SHM_ALLOC) ctx_marker = (volatile u32 *)f[7];
    ctx_state();
    ++ctx_record[14];
}
void ctx_shell_poll(void)
{
    /* Called after idle_work's FG1/reentry guards, before its running flag.
     * Depth 1 is the live resident shell; depth 0 would load ctx as a shell. */
    if (!ctx_can_start(ctx_record[1], exec_nest_level, con_sink_is_enabled())) return;
    ctx_record[1] = 1;
    ctx_start = tick_count;
    (void)exec_run("/bin/ctx.bin");
    int kind, code;
    ctx_check(!exec_last_result(&kind, &code) && kind == EXEC_KIND_EXITED && code == 7);
    ctx_marker = 0;
    ctx_state();
    ctx_record[13] |= 3;
    con_sink_enable();
    int id = exec_start("/bin/ctx.bin child");
    ctx_check(id > 0 && exec_app_state(id) == 4);
    ctx_record[13] |= 8;
    ctx_state();
    ctx_check(exec_resume(id, 0) == 0 && exec_exit_status == 9);
    ctx_state();
    ctx_record[13] |= 16;
    id = exec_start("/bin/ctx.bin child");
    ctx_check(id > 0 && exec_app_state(id) == 4 && !exec_kill(id));
    ctx_state();
    ctx_record[13] |= 4;
    con_sink_disable();
    ctx_record[9] = kstack_high_water() & 0xffff;
    ctx_record[10] = MEM_KSTACK_SIZE;
    ctx_record[11] = !paging_is_present(MEM_STACK_GUARD);
    ctx_record[12] = tick_count - ctx_start;
    ctx_check(ctx_record[9] && ctx_record[9] < ctx_record[10] && ctx_record[11]);
    for (int i = 0; i < 3; ++i) { ctx_check(!(ctx_boundary[i] & 0x200)); ctx_record[6+i] = ctx_boundary[i]; }
    ctx_check(ctx_record[16] && ctx_record[17] > ctx_record[18]);
    ctx_record[1] = 2;
}
#endif
