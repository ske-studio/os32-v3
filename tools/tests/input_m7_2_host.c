#include "input_display_stubs.h"
#include "input_context.h"
static KbdPolicySnapshot foreground = {7, 8, 9, 0, 0};
int input_context_snapshot(KbdPolicySnapshot *out) { *out = foreground; return 0; }
#include "ime_gate.c"
static int probe(void)
{
    KbdPolicyImeSnapshot s;
    kbd_policy_ime_state(1, 7, 9);
    _disable();
    input_ime_snapshot(&s);
    if (_irq_enabled() || s.exec_gen != 8 || input_ime_cancel_capture(&s)) return 1;
    if (input_ime_cancel_capture(&s) || input_ime_cancel_coalesced_count() != 1) return 1;
    foreground.exec_gen = 10;
    if (input_ime_cancel_take(&foreground) != -1 || input_ime_cancel_take(&foreground) != -1) return 1;
    foreground.exec_gen = 8;
    if (input_ime_cancel_capture(&s)) return 1;
    foreground = (KbdPolicySnapshot){6, 11, 1, 0, 0};
    kbd_policy_ime_state(1, 6, 1);
    input_ime_snapshot(&s);
    if (s.exec_gen != 11 || input_ime_cancel_capture(&s) || input_ime_cancel_replaced_count() != 1) return 1;
    if (input_ime_cancel_take(&foreground) || input_ime_cancel_take(&foreground) != -1 || _irq_enabled()) return 1;
    foreground.owner = 7;
    input_ime_snapshot(&s);
    if (s.exec_gen || input_ime_cancel_capture(&s) != -1) return 1;
    _enable();
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
