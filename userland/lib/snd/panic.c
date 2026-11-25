#include "os32api.h"

OS32_KAPI_LAYOUT_STAMP();

/* A user-mode invalid opcode terminates the application. */
__attribute__((noreturn)) void synth_panic(void)
{
    __builtin_trap();
}
