/* Real C boundaries only: fake IF/MSR cannot certify iret/rfi or nested asm. */
#include "types.h"
#include "exec_arch.h"
#include "arch/ppc/ppc_trap.h"
static u32 msr;
volatile u32 kctx_irq_depth;
static int calls, pumps, common_calls, bad_enable, stopped, copy_result;
static void *escape[5];
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define VERIFY(c) do { if (!(c)) finish(1); } while (0)
int _irq_enabled(void) { return !!(msr & PPC_MSR_EE); }
void ppc_enable(void) { msr |= PPC_MSR_EE; }
#ifndef CTX_PPC
u32 ppc_irq_save(void) { u32 old = msr; msr &= ~PPC_MSR_EE; return old; }
#endif
void _idle(void) {}
int net_rx_recent(u32 ms) { (void)ms; return 0; }
#include CTX_IDLE_SOURCE
static void callback(void)
{
    VERIFY(_irq_enabled() && !kctx_irq_depth);
    ++calls;
    VERIFY(calls < 8); /* A missing running guard fails without stack overflow. */
    idle_work_run();
}
static void ring3_gui_pump(void) { ++pumps; }
void exec_syscall_dispatch(struct arch_syscall *call)
{
    VERIFY(call && call->native && !kctx_irq_depth);
    ++common_calls;
    bad_enable = !_irq_enabled();
#include "ctx_entry.inc"
    VERIFY(calls == !bad_enable && pumps == 1);
}
#ifdef CTX_PPC
u32 ppc_trap_msr(void) { return msr; }
u32 ppc_root_current(void) { return 0; }
int ppc_pd_lookup_read(u32 root, u32 va, int store, int user,
    struct ppc_translation *out, ppc_pd_read_fn reader, void *context)
{
    (void)root; (void)va; (void)store; (void)user;
    (void)out; (void)reader; (void)context; return 0;
}
void ppc_trap_fatal_sink(const struct ppc_frame *f, u32 reason)
{ (void)f; (void)reason; finish(1); }
void ppc_trap_stop(void) __attribute__((noreturn));
void ppc_trap_stop(void) { stopped++; __builtin_longjmp(escape, 1); }
void ring3_fault_kill(void) { finish(1); }
#include CTX_TRAP_SOURCE
u32 ppc_irq_save(void)
{
    VERIFY(!probe.active); /* The mask must precede arming onfault. */
    u32 old = msr;
    msr &= ~PPC_MSR_EE;
    return old;
}
void ppc_irq_restore(u32 flags)
{
    VERIFY(!_irq_enabled() && !probe.active);
    msr = flags;
}
const char ppc_probe_copy8_insn[4] = {0}, ppc_probe_copy8_resume[4] = {0};
int ppc_probe_copy8(u32 va, u8 *out)
{
    VERIFY(!_irq_enabled() && probe.active && probe.va == va);
    *out = 7; return copy_result;
}
#include "ctx_onfault.inc"
const char ppc_probe_read_insn[4] = {0}, ppc_probe_read_resume[4] = {0};
const char ppc_probe_exec_resume[4] = {0};
int ppc_probe_read(u32 va)
{
    VERIFY(!_irq_enabled() && probe.active && probe.va == va);
    return copy_result;
}
int ppc_probe_exec(u32 va) { return ppc_probe_read(va); }
#include "ctx_probes.inc"
static void irq(struct ppc_frame *f)
{
    VERIFY(f && kctx_irq_depth == 1 && !_irq_enabled());
    int before = calls;
    idle_work_run(); VERIFY(calls == before);
    /* Independently exercise depth gate even if a caller incorrectly enables. */
    ppc_enable(); idle_work_run(); VERIFY(calls == before);
    (void)ppc_irq_save();
}
#else
int legacy_trap(void *native) { VERIFY(native); return 0; }
#include CTX_TRAP_SOURCE
#endif
void _start(void)
{
    VERIFY(idle_work_register(callback) == 0);
    msr = PPC_MSR_IR | PPC_MSR_DR;
    if (CTX_VECTOR == 1) {
        idle_work_run(); VERIFY(calls == 0); /* FG0 */
        ppc_enable(); kctx_irq_depth = 1;
        idle_work_run(); VERIFY(calls == 0); /* IRQ with IF/EE=1 */
        kctx_irq_depth = 0;
        idle_work_run(); VERIFY(calls == 1 && _irq_enabled());
        idle_work_run(); VERIFY(calls == 2); /* running restored */
    }
#ifdef CTX_PPC
    struct ppc_frame f = { .vector = 0xc00, .srr1 = PPC_MSR_PR };
    if (CTX_VECTOR == 2) {
        const u32 vectors[] = {0x500, PPC_VECTOR_DEC};
        for (unsigned i = 0; i < 2; ++i) {
            f.vector = vectors[i];
            ppc_trap_set_handler(f.vector, irq); ppc_trap_dispatch(&f);
            VERIFY(kctx_irq_depth == 0 && !_irq_enabled());
        }
    }
    if (CTX_VECTOR == 3) {
        const u32 invalid[] = {PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_EE,
                              PPC_MSR_IR, PPC_MSR_DR, 0};
        for (unsigned i = 0; i < 4; ++i) {
            msr = invalid[i]; stopped = 0;
            if (!__builtin_setjmp(escape)) ppc_trap_dispatch(&f);
            VERIFY(stopped == 1 && common_calls == 0 && !kctx_irq_depth);
        }
    }
    if (CTX_VECTOR == 4) {
        ppc_trap_dispatch(&f);
        VERIFY(common_calls == 1 && !_irq_enabled() && !kctx_irq_depth);
        if (bad_enable) finish(42); /* Only the known pre-W4c enable gap. */
    }
    if (CTX_VECTOR == 5) {
        ppc_enable();
        for (copy_result = 0; copy_result < 2; ++copy_result) {
            VERIFY(probe_dsi(0x1000) == (copy_result == 1));
            VERIFY(_irq_enabled() && !probe.active);
            VERIFY(probe_isi(0x1000) == (copy_result == 1));
            VERIFY(_irq_enabled() && !probe.active);
            u8 out = 0;
            VERIFY(pm_lan_copy8(0x1000, &out) == copy_result);
            VERIFY(out == 7 && _irq_enabled() && !probe.active);
        }
        (void)ppc_irq_save();
        u8 out;
        VERIFY(pm_lan_copy8(0x1000, &out) == copy_result);
        VERIFY(!_irq_enabled() && !probe.active);
    }
#else
    (void)escape; (void)stopped; (void)copy_result;
    if (CTX_VECTOR == 4) {
        u32 frame[13] = {0};
        ppc_enable(); /* x86 STI is assembly: this is an explicit precondition. */
        ring3_syscall_dispatch(frame);
        VERIFY(common_calls == 1 && !bad_enable && _irq_enabled());
    }
#endif
    finish(0);
}
