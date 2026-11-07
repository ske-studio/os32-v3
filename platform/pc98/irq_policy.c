/* PC98 fixed stubs and dynamic registration order; no I/O. */
#include "platform_irq.h"
#include "irq_math.h"
#include "pc98.h"

static const unsigned dynamic_lines[PLATFORM_IRQ_DYNAMIC_COUNT] = {
    3, 5, 6, 8, 9, 10, 11, 14, 15
};
static const struct irq_topology topology = {
    PLATFORM_IRQ_LINES, PLATFORM_IRQ_DYNAMIC_COUNT, dynamic_lines
};
const struct irq_topology *platform_irq_topology(void) { return &topology; }
int platform_irq_reserved(unsigned line)
{
    return irq_dyn_index_in(&topology, line) < 0;
}

int irq_eoi_plan(unsigned int irq, int slave_isr_bit7)
{
    if (irq >= (unsigned int)IRQ_LINE_MAX) return IRQ_EOI_NONE;

    if (irq == PIC_SLAVE_LAST_IRQ) {
        /* スレーブ側スプリアス: ISR の IR7 が立っていなければスレーブへは
         * 送らない (カスケード分のマスタだけ)。**handled とは無関係** —
         * 受け手が居たかどうかでスプリアスかどうかは決まらない。 */
        return slave_isr_bit7 ? IRQ_EOI_SLAVE_MASTER : IRQ_EOI_MASTER;
    }
    return (irq >= PIC_IRQS) ? IRQ_EOI_SLAVE_MASTER : IRQ_EOI_MASTER;
}
