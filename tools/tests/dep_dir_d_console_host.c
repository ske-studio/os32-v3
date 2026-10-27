/* Same input/output seam for the pre-move fixture and the current backend. */
#include "types.h"
#include "kprintf.h"
#ifndef BASELINE
#include "console_attr.h"
#endif

static u8 output[U8_MAX + 1];

static void finish(int status)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status) : "memory");
    __builtin_unreachable();
}

void _start(void)
{
    for (unsigned input = 0; input <= U8_MAX; input++) {
        output[input] = kprintf_attr_to_pc98((u8)input);
#ifndef BASELINE
        if (output[input] != con_backend_printf_attr((u8)input)) finish(1);
#endif
    }
    int written;
    __asm__ volatile("int $0x80" : "=a"(written)
                     : "a"(4), "b"(1), "c"(output), "d"(sizeof(output))
                     : "memory");
    finish(written != (int)sizeof(output));
}
