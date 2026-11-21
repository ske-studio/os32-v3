#include "input_display_stubs.h"
#include "input_context.h"
static u32 generation = 8;
static int missing;
int input_context_snapshot(KbdPolicySnapshot *out)
{
    *out = (KbdPolicySnapshot){7, generation, 9, 0, 0};
    return missing;
}
#include "ime_gate.c"

static int probe(void)
{
    KbdPolicyImeSnapshot s;
    _enable();
    kbd_policy_ime_state(1, 7, 9);
    generation++;
    input_ime_snapshot(&s);
    if (!_irq_enabled() || s.exec_gen != 8 || s.active != 1 || s.owner != 7 || s.command_gen != 9) return 1;
    _disable();
    kbd_policy_ime_state(0, 4, 12);
    input_ime_snapshot(&s);
    if (_irq_enabled() || s.active != 0 || s.owner || s.command_gen || s.exec_gen) return 1;
    missing = -1;
    kbd_policy_ime_state(1, 7, 9);
    input_ime_snapshot(&s);
    if (s.active || s.owner || s.exec_gen || s.command_gen) return 1;
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
