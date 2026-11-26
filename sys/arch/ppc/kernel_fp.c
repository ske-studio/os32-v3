/* PPC_FP_NEWLIB_DESIGN §2; explicit, single-CPU FP interval. */
#include "arch_io.h"
#include "arch_kernel_fp.h"
#include "ppc_oea.h"
#include "sys/kern/irq/irq.h"
#include "sys/kern/mm/pgalloc.h"

static const struct ppc_kernel_fp_guard *active;
int arch_kernel_fp_begin(struct arch_kernel_fp_guard *g)
{
    return ppc_kernel_fp_begin(g ? &g->ppc : 0);
}

void arch_kernel_fp_end(const struct arch_kernel_fp_guard *g)
{
    ppc_kernel_fp_end(&g->ppc);
}

static void set_msr(u32 value)
{
    __asm__ volatile("sync; mtmsr %0; isync" : : "r"(value) : "memory");
}

int ppc_kernel_fp_begin(struct ppc_kernel_fp_guard *g)
{
    u32 old = ppc_irq_save();
    if (!g || active || kctx_irq_depth || kctx_exc_depth) {
        ppc_irq_restore(old);
        return -1;
    }
    active = g;
    g->previous_msr = old;
    arch_fp_save(&g->previous);
    set_msr((old & ~(PPC_MSR_EE | PPC_MSR_FE)) | PPC_MSR_FP);
    arch_fp_init();
    set_msr((old & ~PPC_MSR_FE) | PPC_MSR_FP);
    return 0;
}

void ppc_kernel_fp_end(const struct ppc_kernel_fp_guard *g)
{
    (void)ppc_irq_save();
    arch_fp_restore(&g->previous);
    active = 0;
    set_msr(g->previous_msr);
}
