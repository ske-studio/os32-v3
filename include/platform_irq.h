/* Link-time IRQ controller boundary. Fixed stubs keep their native completion. */
#ifndef KERNEL_PLATFORM_IRQ_H
#define KERNEL_PLATFORM_IRQ_H

#include <platform_irq_limits.h>

/* Topology is fixed after boot. Query at any IF; operations require IF=0. */
struct irq_topology { unsigned lines, dynamic_count; const unsigned *dynamic_lines; };
const struct irq_topology *platform_irq_topology(void);
int platform_irq_reserved(unsigned line);
void platform_irq_mask(unsigned line);
void platform_irq_unmask(unsigned line);
struct irq_claim { unsigned line, native_token; int deliver, needs_finish; };
/* Success=1; invalid token/output returns negative without modifying output. */
int platform_irq_claim(unsigned native_token, struct irq_claim *out);
void platform_irq_complete(const struct irq_claim *claim);

/* IRQ15 finish, IF=0: read ISR bit7 and restore IRR selection. */
int platform_irq_slave_last_in_service(void);
/* IRQ context: acknowledge only the master. */
void platform_irq_eoi_master(void);
/* Caller IF=0: select/read master IRR bit0 (used by L13). */
int platform_irq_timer_pending(void);

#endif /* KERNEL_PLATFORM_IRQ_H */
