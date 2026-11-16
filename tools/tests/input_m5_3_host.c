#include "context.c"
static int calls;
static void discard(void) { calls++; }
static int probe(void)
{
    KbdPolicySnapshot s, t;
    _enable();
    if (!input_context_snapshot(&s) || !input_context_first_shell(0)) return 1;
    if (input_context_first_shell(7) || !input_context_first_shell(8)) return 1;
    if (input_context_snapshot(&s) || s.owner != 1 || s.exec_gen != 7 ||
        s.command_gen || s.context != INPUT_CONTEXT_LINE_EDIT || s.flags != 3) return 1;
    if (input_context_owner_register(2, 11, 1, 7)) return 1;
    if (input_context_snapshot(&t) || t.owner != 1) return 1;
    if (input_context_owner_select(2, 12) == 0 || input_context_owner_select(2, 11)) return 1;
    if (input_context_snapshot(&s) || s.owner != 2 || s.exec_gen != 11 ||
        s.context != INPUT_CONTEXT_RUNNING || s.flags != INPUT_CONTEXT_ISIG) return 1;
    input_context_discard_register(discard);
    if (input_context_owner_begin(2, 11) || calls != 1) return 1;
    _disable();
    if (input_context_snapshot(&s) || _irq_enabled() || s.command_gen != 1) return 1;
    if (input_context_snapshot(&t) || t.owner != s.owner || t.exec_gen != s.exec_gen ||
        t.command_gen != s.command_gen || t.context != s.context || calls != 1) return 1;
    _enable();
    if (input_context_owner_end(2, 11) || input_context_owner_release(2, 11)) return 1;
    if (input_context_snapshot(&s) || s.owner != 1 || s.context != INPUT_CONTEXT_LINE_EDIT) return 1;
    if (input_context_owner_register(2, 12, 1, 7) || input_context_owner_select(2, 12)) return 1;
    if (input_context_owner_set(2, 11, INPUT_CONTEXT_GUI, 0) == 0 ||
        input_context_owner_release(2, 11) == 0) return 1;
    if (input_context_snapshot(&s) || s.exec_gen != 12 || s.context != INPUT_CONTEXT_RUNNING) return 1;
    if (input_context_owner_release(1, 7) || input_context_owner_register(1, 8, 0, 0)) return 1;
    if (input_context_owner_release(2, 12) || !input_context_snapshot(&s)) return 1;
    if (!input_context_owner_register(APP_SLOT_COUNT, 1, 0, 0) ||
        !input_context_owner_register(0, 1, 0, 0) ||
        !input_context_owner_register(3, 1, 1, 7)) return 1;
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
