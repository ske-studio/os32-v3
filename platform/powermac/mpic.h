/* CPU0 MPIC boundary. All calls require DR=1 and EE=0, except self IPI.
 * The caller owns an I/G alias and must stop boot on init failure. */
#ifndef OS32_POWERMAC_MPIC_H
#define OS32_POWERMAC_MPIC_H
#include "mpic_tree.h"
#include "ppc_frame.h"
#define PM_MPIC_MAX_SOURCES 128U
#define PM_MPIC_IPI_VECTOR 254U
#define PM_MPIC_SPURIOUS 255U
#define PM_MPIC_DRAIN_LIMIT 256U
#define PM_MPIC_WINDOW_MIN 0x200b4U
#define PM_MPIC_WINDOW_MAX 0x40000U
/* E2 private claim, to be adapted to the common IRQ contract in E3.
 * IPI is a native vector, never a source bitmap line. */
struct irq_claim { unsigned native_token; };
enum pm_mpic_error {
    PM_MPIC_INIT_OK, PM_MPIC_WINDOW, PM_MPIC_ORDER,
    PM_MPIC_SOURCES, PM_MPIC_DRAIN
};
int pm_mpic_init(u32 va, u32 bytes, int mmio_order);
void platform_boot_irq_controller(void);
int platform_irq_claim(unsigned native_token, struct irq_claim *out);
void platform_irq_complete(const struct irq_claim *claim);
void pm_mpic_external(struct ppc_frame *frame);
/* n is IPI index: only reserved IPI0 is available. */
int pm_mpic_send_self_ipi(u32 n);
void pm_mpic_set_ipi_handler(void (*handler)(struct ppc_frame *));
u32 pm_mpic_source_count(void);
#endif
