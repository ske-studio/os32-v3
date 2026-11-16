/* Expectation origin: PPC_FP_NEWLIB_DESIGN §8 F3. MSR/FPR hardware is fake;
 * the real guard C control flow executes, without libc. */
#include "kernel_fp.h"
#include "ppc_oea.h"
volatile u32 kctx_irq_depth, kctx_exc_depth;
static u32 msr;
static struct arch_fp_context hardware;
static void fake_set_msr(u32 value) { msr = value; }
u32 ppc_irq_save(void) { u32 old = msr; msr &= ~PPC_MSR_EE; return old; }
void ppc_irq_restore(u32 old) { msr = old; }
void arch_fp_save(struct arch_fp_context *p) { *p = hardware; }
void arch_fp_restore(const struct arch_fp_context *p) { hardware = *p; }
void arch_fp_init(void)
{
    for (u32 i = 0; i < ARCH_FP_BYTES; ++i) hardware.opaque[i] = 0;
}
#include "guard.inc"
static int check(void)
{
    struct ppc_kernel_fp_guard a, b;
    u32 old = PPC_MSR_EE | PPC_MSR_FE | PPC_MSR_IR | PPC_MSR_DR;
    msr = old;
    for (u32 i = 0; i < ARCH_FP_BYTES; ++i) hardware.opaque[i] = (i + 1) & 255;
    kctx_irq_depth = 1;
    if (!ppc_kernel_fp_begin(&a) || msr != old) return 10;
    kctx_irq_depth = 0; kctx_exc_depth = 1;
    if (!ppc_kernel_fp_begin(&a) || msr != old) return 11;
    kctx_exc_depth = 0;
    if (!ppc_kernel_fp_begin(0) || msr != old) return 12;
    if (ppc_kernel_fp_begin(&a) || msr != ((old & ~PPC_MSR_FE) | PPC_MSR_FP)) return 13;
    for (u32 i = 0; i < ARCH_FP_BYTES; ++i) if (hardware.opaque[i]) return 14;
    if (!ppc_kernel_fp_begin(&b)) return 15;
    for (u32 i = 0; i < ARCH_FP_BYTES; ++i) hardware.opaque[i] = 0x5a;
    ppc_kernel_fp_end(&a);
    if (msr != old) return 16;
    for (u32 i = 0; i < ARCH_FP_BYTES; ++i)
        if (hardware.opaque[i] != ((i + 1) & 255)) return 3;
    if (ppc_kernel_fp_begin(&a)) return 17;
    ppc_kernel_fp_end(&a);
    return 0;
}
void _start(void)
{
    int rc = check();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
