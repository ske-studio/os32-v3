/* MPC7450UM 4.6.3 table 4-10, 4.6.4; OpenBSD powerpc/trap.c:266-299
 * (spill precedes ordinary fault; consume onfault before returning).
 * No demand paging, x86 frame handlers, or implicit platform dependencies. */
#include "ppc_trap.h"
#include "ppc_hpt.h"
#include "exec_arch.h"
#include "kernel/irq/irq.h"

extern void ring3_fault_kill(void);
static void (*external_handler)(struct ppc_frame *);
static void (*dec_handler)(struct ppc_frame *);
static ppc_pd_read_fn table_reader;
static void *table_context;
static int fatal_ready;
static struct { u32 pc, va, reason, resume; int active, data; } probe;
#ifdef PPC_TRAP_HOST_TEST
u32 ppc_trap_msr(void);
void ppc_trap_stop(void) __attribute__((noreturn));
#else
static u32 ppc_trap_msr(void)
{
    u32 value;
    __asm__ volatile("mfmsr %0" : "=r"(value));
    return value;
}
static void ppc_trap_stop(void) __attribute__((noreturn, noinline));
static void ppc_trap_stop(void)
{
    for (;;) __asm__ volatile("b ." : : : "memory");
}
#endif
void ppc_trap_set_handler(u32 vector, void (*handler)(struct ppc_frame *))
{
    if (vector == 0x500) external_handler = handler;
    if (vector == PPC_VECTOR_DEC) dec_handler = handler;
}
void ppc_trap_set_reader(ppc_pd_read_fn reader, void *context)
{
    table_reader = reader;
    table_context = context;
}
void ppc_trap_set_fatal_ready(int ready) { fatal_ready = !!ready; }
void ppc_trap_onfault_clear(void) { probe.active = 0; }
int ppc_trap_onfault(u32 pc, u32 va, u32 reason, u32 resume)
{
    if (probe.active || ((pc | resume) & 3) ||
        (reason != PPC_TRAP_DSI_MISS && reason != PPC_TRAP_DSI_PROTECT &&
         reason != PPC_TRAP_DSI_IO && reason != PPC_TRAP_ISI &&
         reason != PPC_TRAP_ISI_PROTECT && reason != PPC_TRAP_ISI_NOEXEC)) return 0;
    probe.pc = pc; probe.va = va; probe.reason = reason; probe.resume = resume;
    probe.data = 0;
    probe.active = 1;
    return 1;
}
int ppc_trap_onfault_data(u32 pc, u32 va, u32 resume)
{
    if (probe.active || ((pc | resume) & 3)) return 0;
    probe.pc = pc; probe.va = va; probe.resume = resume;
    probe.data = 1;
    probe.active = 1;
    return 1;
}
static u32 fault_reason(const struct ppc_frame *f)
{
    struct ppc_translation t;
    u32 va = f->vector == PPC_VECTOR_ISI ? f->srr0 : f->dar;
    /* DR=1: use the explicitly registered mapped reader, never pd_read_real. */
    if (!table_reader || !ppc_root_current()) return PPC_TRAP_UNKNOWN;
    int mapped = ppc_pd_lookup_read(ppc_root_current(), va,
                           f->vector == PPC_VECTOR_DSI && !!(f->dsisr & PPC_DSISR_STORE),
                           0, &t, table_reader, table_context);
    if (f->vector == PPC_VECTOR_ISI) {
        u32 cause = f->srr1 & 0x78000000;
        if (cause == PPC_ISI_MISS) return PPC_TRAP_ISI;
        if (cause == 0x08000000) return PPC_TRAP_ISI_PROTECT;
        if (cause == 0x10000000) return PPC_TRAP_ISI_NOEXEC;
        return PPC_TRAP_UNKNOWN;
    }
    u32 cause = f->dsisr & ~PPC_DSISR_STORE;
    if (cause == PPC_DSISR_PROTECT) return PPC_TRAP_DSI_PROTECT;
    /* Atomic operation on I/W memory, or EAR-disabled external access. */
    if (cause == 0x04000000 || cause == 0x00100000)
        return PPC_TRAP_DSI_IO;
    if (cause == PPC_DSISR_MISS && !mapped) return PPC_TRAP_DSI_MISS;
    return PPC_TRAP_UNKNOWN;
}
void ppc_trap_dispatch(struct ppc_frame *f)
{
    u32 reason = PPC_TRAP_UNKNOWN;
    u32 msr = ppc_trap_msr();
    int translated = (msr & (PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_EE)) ==
                     (PPC_MSR_IR | PPC_MSR_DR);
    if (!f || !translated) goto fatal;
    if (f->vector == 0x500 || f->vector == PPC_VECTOR_DEC) {
        void (*handler)(struct ppc_frame *) =
            f->vector == 0x500 ? external_handler : dec_handler;
        if (handler) {
            ++kctx_irq_depth;
            handler(f);
            --kctx_irq_depth;
            return;
        }
        reason = PPC_TRAP_UNREGISTERED;
    } else if (f->srr1 & PPC_MSR_PR) {
        if (f->vector == 0xc00) {
            struct arch_syscall call = { f };
            exec_syscall_dispatch(&call);
            return;
        }
        ring3_fault_kill();
        reason = PPC_TRAP_USER | PPC_TRAP_UNSUPPORTED;
    } else if (f->vector == PPC_VECTOR_DSI || f->vector == PPC_VECTOR_ISI) {
        reason = fault_reason(f);
        if (probe.active && f->srr0 == probe.pc &&
            (f->vector == PPC_VECTOR_ISI ? f->srr0 : f->dar) == probe.va &&
            (probe.data ? (reason == PPC_TRAP_DSI_MISS ||
                           reason == PPC_TRAP_DSI_PROTECT ||
                           reason == PPC_TRAP_DSI_IO) : reason == probe.reason)) {
            probe.active = 0;
            f->srr0 = probe.resume;
            return;
        }
    } else if (f->vector == 0x700) {
        /* PEM table 6-16: preserve the program exception cause. */
        u32 cause = f->srr1 & 0x001e0000;
        reason = PPC_TRAP_UNKNOWN | PPC_TRAP_UNSUPPORTED;
        if (cause == 0x00100000) reason = PPC_TRAP_PROGRAM_FP | PPC_TRAP_UNSUPPORTED;
        if (cause == 0x00080000) reason = PPC_TRAP_PROGRAM_ILLEGAL | PPC_TRAP_UNSUPPORTED;
        if (cause == 0x00040000) reason = PPC_TRAP_PROGRAM_PRIVILEGED | PPC_TRAP_UNSUPPORTED;
        if (cause == 0x00020000) reason = PPC_TRAP_PROGRAM_TRAP | PPC_TRAP_UNSUPPORTED;
    } else if (f->vector == 0x600) {
        reason = PPC_TRAP_ALIGNMENT;
    } else if (f->vector == 0xc00) {
        reason = PPC_TRAP_SC | PPC_TRAP_UNSUPPORTED;
    }
fatal:
    probe.active = 0;
    if (f && translated && fatal_ready && ppc_root_current())
        ppc_trap_fatal_sink(f, reason);
    ppc_trap_stop();
}
