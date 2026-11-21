#include "ppc_trap.h"
/* Fixture owns IRQ context storage; no shared product definition. */
volatile u32 kctx_irq_depth;
#include "exec_arch.h"
static void *escape[5];
static int seen;
static u32 diagnostic;
static u32 trap_msr = 0x30;
void ppc_enable(void) { trap_msr |= PPC_MSR_EE; }
u32 ppc_irq_save(void) { u32 old = trap_msr; trap_msr &= ~PPC_MSR_EE; return old; }
u32 ppc_trap_msr(void) { return trap_msr; }
u32 ppc_root_current(void) { return 0x1000; }
void ppc_trap_stop(void) { seen = 3; __builtin_longjmp(escape, 1); }
void ppc_trap_fatal_sink(const struct ppc_frame *f, u32 reason) { (void)f; diagnostic = reason; }
void exec_syscall_dispatch(struct arch_syscall *call)
{
    struct ppc_frame *f = call->native;
    seen = 1;
    f->gpr[3] = 42;
}
void ppc_user_fault_kill(void) { seen = 2; __builtin_longjmp(escape, 1); }
static void irq(struct ppc_frame *f) { (void)f; seen = 4; }
static void resident(struct ppc_frame *f)
{
    (void)f;
    seen = 5;
    __builtin_longjmp(escape, 1);
}
static void fault_in_irq(struct ppc_frame *f)
{
    struct ppc_frame nested = { .vector = 0x700 };
    (void)f;
    ppc_trap_dispatch(&nested);
}
int main(void)
{
    struct ppc_frame f = { .vector = 0xc00, .srr1 = PPC_MSR_PR, .srr0 = 0x104 };
    if (!__builtin_setjmp(escape)) ppc_trap_dispatch(&f);
    if (seen != 1 || f.gpr[3] != 42 || f.srr0 != 0x104) {
        const char marker[] = "SC-RED\n";
        __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(marker), "d"(sizeof(marker) - 1) : "memory");
        return 10;
    }
    seen = 0; f.vector = PPC_VECTOR_DSI;
    if (!__builtin_setjmp(escape)) ppc_trap_dispatch(&f);
    if (seen != 2) return 11;
    seen = 0; f.srr1 = 0;
    if (!__builtin_setjmp(escape)) ppc_trap_dispatch(&f);
    if (seen != 3) return 12;
    seen = 0; f.srr1 = PPC_MSR_PR; f.vector = PPC_VECTOR_DEC;
    ppc_trap_set_handler(PPC_VECTOR_DEC, irq); ppc_trap_dispatch(&f);
    if (seen != 4) return 13;
    ppc_trap_set_fatal_ready(1);
    seen = 0; f.vector = 0x800; f.srr1 = PPC_MSR_PR;
    if (!__builtin_setjmp(escape)) ppc_trap_dispatch(&f);
    if (seen != 2 || diagnostic != PPC_TRAP_FP_UNAVAILABLE) return 14;
    seen = 0; diagnostic = 0; f.srr1 = 0;
    if (!__builtin_setjmp(escape)) ppc_trap_dispatch(&f);
    if (seen != 3 || diagnostic != PPC_TRAP_FP_UNAVAILABLE) return 15;
    ppc_trap_set_resident_fault(resident);
    f.vector = 0x700;
    if (!__builtin_setjmp(escape)) ppc_trap_dispatch(&f);
    if (seen != 5) return 16;
    f.vector = 0x500;
    if (!__builtin_setjmp(escape)) ppc_trap_dispatch(&f);
    if (seen != 3 || diagnostic != PPC_TRAP_UNREGISTERED) return 17;
    ppc_trap_set_handler(0x500, fault_in_irq);
    if (!__builtin_setjmp(escape)) ppc_trap_dispatch(&f);
    if (seen != 3 || kctx_irq_depth != 1) return 18;
    return 0;
}
void _start(void) { int rc = main(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
