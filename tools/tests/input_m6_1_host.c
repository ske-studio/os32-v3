#include "context.c"
#include "interrupt.c"

static int probe(void)
{
    KbdPolicyContext s;
    input_context_init(&s);
    input_interrupt_init();
    interrupt_pending = (InputInterruptPending){1, 2, 3, 4};
    display_restore_pending = (InputDisplayRestorePending){5, 6};
    _enable();
    if (input_context_command_begin(&s) || !_irq_enabled()) return 1;
    if (interrupt_pending.owner || interrupt_pending.exec_gen ||
        interrupt_pending.command_gen || interrupt_pending.reason ||
        display_restore_pending.owner != 5 || display_restore_pending.resource_gen != 6) return 1;
    interrupt_pending = (InputInterruptPending){1, 2, s.command_gen, 4};
    _disable();
    if (input_context_command_end(&s) || _irq_enabled()) return 1;
    if (interrupt_pending.owner || interrupt_pending.exec_gen ||
        interrupt_pending.command_gen || interrupt_pending.reason ||
        display_restore_pending.owner != 5 || display_restore_pending.resource_gen != 6) return 1;
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
