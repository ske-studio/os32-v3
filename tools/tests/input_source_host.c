#include "in2_int.h"
#include "os32_kapi_shared.h"
static int probe(void)
{
    u16 gen[16], next;
    if (sizeof(InputEvent)!=16 || __builtin_offsetof(InputEvent, src)!=6 ||
        __builtin_offsetof(InputEvent, type)!=7 || __builtin_offsetof(InputEvent, x)!=8 ||
        __builtin_offsetof(InputEvent, y)!=10 || __builtin_offsetof(InputEvent, sc)!=12 ||
        __builtin_offsetof(InputEvent, down)!=13 || __builtin_offsetof(InputEvent, buttons)!=14 ||
        __builtin_offsetof(InputEvent, flags)!=15 || __builtin_offsetof(InputEvent, generation)!=4 ||
        __builtin_offsetof(InputEvent, time_ms)!=0) return 1;
    for (int i=0; i<16; ++i)
        if (input_src_register(1, 2, &gen[i]) != i+1) return 2;
    if (input_src_register(1, 2, &next) != OS32_ERR_FULL) return 3;
    input_src_release(0, gen[0]); input_src_release(17, gen[0]);
    if (input_src_register(1, 2, &next) != OS32_ERR_FULL) return 4;
    input_src_release(5, gen[4]);
    if (input_src_register(2, 3, &next) != 5 || next != (u16)(gen[4]+1)) return 5;
    input_src_release(5, gen[4]);
    if (input_src_register(1, 2, &next) != OS32_ERR_FULL) return 6;
    return 0;
}
#ifndef PPC_COMPILE
void _start(void) {
    int rc=probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#else
int fixture(void) { return probe(); }
#endif
