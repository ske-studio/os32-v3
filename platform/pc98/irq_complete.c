/* Only the dynamic C path owns completion here. Fixed stubs retain their EOI. */
#include "platform_irq.h"
#include "irq_math.h"
#include "idt.h"

void platform_irq_mask(unsigned line) { irq_disable(line); }
void platform_irq_unmask(unsigned line) { irq_enable(line); }

int platform_irq_claim(unsigned native_token, struct irq_claim *out)
{
    if (!out || native_token >= PLATFORM_IRQ_LINES ||
        platform_irq_reserved(native_token)) return IRQ_ERR_INVAL;
    *out = (struct irq_claim){native_token, native_token, 1, 1};
    return 1;
}

void platform_irq_complete(const struct irq_claim *claim)
{
    int bit7 = 0;
    int plan;
    if (!claim->needs_finish) return;
    if (claim->line == PIC_SLAVE_LAST_IRQ)
        bit7 = platform_irq_slave_last_in_service();
    plan = irq_eoi_plan(claim->line, bit7);
    if (plan == IRQ_EOI_SLAVE_MASTER) {
        pic_eoi(claim->line);
    } else if (plan == IRQ_EOI_MASTER) {
        platform_irq_eoi_master();
    }
}
