#include "input_display_stubs.h"
#include "input_context.h"
int input_context_snapshot(KbdPolicySnapshot *out)
{
    *out = (KbdPolicySnapshot){7, 8, 9, 0, 0};
    return 0;
}
#include "ime_gate.c"

static int probe(void)
{
    KbdPolicyImeSnapshot s;
    _enable();
    kbd_policy_ime_state(1, 7, 9);
    input_ime_snapshot(&s);
    if (!_irq_enabled() || s.active != 1 || s.owner != 7 || s.command_gen != 9) return 1;
    _disable();
    kbd_policy_ime_state(0, 4, 12);
    input_ime_snapshot(&s);
    if (_irq_enabled() || s.active != 0 || s.owner != 4 || s.command_gen != 12) return 1;
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
