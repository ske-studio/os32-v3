/* Shared host vectors: provider selection and privileged operations are seams. */
#include "types.h"
#include "kernel/irq/tick.h"
volatile u32 tick_count;
static u32 irq_state = 1;
u32 irq_save(void) { u32 old = irq_state; irq_state = 0; return old; }
void irq_restore(u32 old) { irq_state = old; }
u32 ppc_irq_save(void) { return irq_save(); }
void ppc_irq_restore(u32 old) { irq_restore(old); }
void _enable(void) { irq_state = 1; }
void _halt(void) {}
void _idle(void) { irq_state = 1; }
void idle_work_run(void) {}
int tick_register(unsigned int slot, tick_fn fn) { (void)slot; (void)fn; return 0; }
#include PROVIDER_SOURCE

static int time3(void)
{
    static const u32 inputs[] = { 0, 3, 0xfffffffdu, 0xffffffffu };
    static const u32 expected[] = { 0, 3, 0xfffffffdu, 0xffffffffu };
    unsigned int i;
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        tick_count = inputs[i];
        if (link_platform_now() != expected[i]) return 1;
    }
    return 0;
}
void _start(void)
{
    int rc = time3();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
