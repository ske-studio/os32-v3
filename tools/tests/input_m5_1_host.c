#include "input_context.h"
static int probe(void)
{
    InputContext s;
    input_context_init(&s);
    if (s.context != INPUT_CONTEXT_RUNNING || s.flags != INPUT_CONTEXT_ISIG || s.command_gen != 0) return 1;
    s.command_gen = 42;
    if (input_context_set(&s, INPUT_CONTEXT_LINE_EDIT, INPUT_CONTEXT_ISIG | INPUT_CONTEXT_ICANON)) return 1;
    if (s.context != INPUT_CONTEXT_LINE_EDIT || s.flags != 3 || s.command_gen != 42) return 1;
    if (input_context_set(&s, INPUT_CONTEXT_BYTE_INPUT, 0) || s.flags || s.command_gen != 42) return 1;
    if (input_context_set(&s, INPUT_CONTEXT_GUI, INPUT_CONTEXT_ISIG) || s.context != INPUT_CONTEXT_GUI) return 1;
    if (!input_context_set(&s, 0, 0) || !input_context_set(&s, 5, 0) || !input_context_set(&s, 1, 4)) return 1;
    return s.context != INPUT_CONTEXT_GUI || s.flags != INPUT_CONTEXT_ISIG || s.command_gen != 42;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
