#include "context.c"
#include "interrupt.c"

static int probe(void)
{
    KbdPolicyContext state;
    KbdPolicySnapshot a = {1, 2, 3, INPUT_CONTEXT_RUNNING, INPUT_CONTEXT_ISIG};
    KbdPolicySnapshot b = a;
    u32 reason = 0;
    input_context_init(&state);
    input_interrupt_init();
    _enable();
    if (input_interrupt_capture(&a, 7) || input_interrupt_capture(&a, 8) ||
        input_interrupt_coalesced_count() != 1 || !_irq_enabled()) return 1;
    if (input_interrupt_take(&a, &reason) || reason != 7 ||
        !input_interrupt_take(&a, &reason)) return 1;
    for (int i = 0; i < 3; ++i) {
        b = a;
        if (i == 0) ++b.owner;
        if (i == 1) ++b.exec_gen;
        if (i == 2) ++b.command_gen;
        if (input_interrupt_capture(&a, 7) || !input_interrupt_take(&b, &reason) ||
            !input_interrupt_take(&a, &reason)) return 1;
    }
    if (input_interrupt_capture(&a, 7) || input_interrupt_capture(&b, 9) ||
        input_interrupt_coalesced_count() != 2) return 1;
    if (input_context_command_begin(&state) || !input_interrupt_take(&a, &reason)) return 1;
    a.command_gen = state.command_gen;
    _disable();
    if (input_interrupt_capture(&a, 11) || _irq_enabled() ||
        input_interrupt_take(&a, &reason) || reason != 11 || _irq_enabled()) return 1;
    if (!input_interrupt_capture(0, 1) || !input_interrupt_capture(&a, 0) ||
        !input_interrupt_take(0, &reason) || !input_interrupt_take(&a, 0)) return 1;
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
