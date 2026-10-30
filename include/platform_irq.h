/* Internal, link-time PIC boundary. IRQ policy stays in kernel/irq.c. */
#ifndef KERNEL_PLATFORM_IRQ_H
#define KERNEL_PLATFORM_IRQ_H

/* IRQ15 finish, IF=0: read ISR bit7 and restore IRR selection. */
int platform_irq_slave_last_in_service(void);
/* IRQ context: acknowledge only the master. */
void platform_irq_eoi_master(void);
/* Caller IF=0: select/read master IRR bit0 (used by L13). */
int platform_irq_timer_pending(void);

#endif /* KERNEL_PLATFORM_IRQ_H */
