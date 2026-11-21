#include "context.c"
#include "io.h"

static KbdPolicyContext *observed;
static u32 expected_gen;
static int calls, bad;
static void discard(void)
{
    ++calls;
    if (_irq_enabled() || observed->command_gen != expected_gen) bad = 1;
}
static int probe(void)
{
    KbdPolicyContext s;
    observed = &s;
    input_context_init(&s);
    input_context_set(&s, INPUT_CONTEXT_LINE_EDIT, 3);
    _enable();
    if (!input_context_command_begin(&s)) return 1;
    input_context_discard_register(discard);
    if (input_context_command_begin(&s) || calls != 1 || bad || !_irq_enabled()) return 1;
    if (s.command_gen != 1 || s.context != INPUT_CONTEXT_RUNNING || s.flags != 1) return 1;
    if (!input_context_command_begin(&s) || calls != 1) return 1;
    input_context_set(&s, INPUT_CONTEXT_BYTE_INPUT, 0);
    expected_gen = 1;
    if (input_context_command_end(&s) || calls != 2 || bad || s.command_gen ||
        s.context != INPUT_CONTEXT_LINE_EDIT || s.flags != 3) return 1;
    if (!input_context_command_end(&s)) return 1;
    s.next_gen = 0xffffffffu;
    expected_gen = 0;
    if (input_context_command_begin(&s) || s.command_gen != 1 || calls != 3 || bad) return 1;
    expected_gen = 1;
    _disable();
    if (input_context_command_end(&s) || _irq_enabled() || calls != 4 || bad) return 1;
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
