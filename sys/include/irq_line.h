#ifndef OS32_IRQ_LINE_H
#define OS32_IRQ_LINE_H
/* 割り込みの線の許可と禁止。ドライバが自分の線に使う口で、実体は割り込みコントローラを持つ側
 * (x86-pc98 は sys/arch/x86/kernel/idt.c の 8259A) が出す。登録は sys/kern/irq/irq.h。 */
void irq_enable(unsigned int irq);
void irq_disable(unsigned int irq);
#endif
