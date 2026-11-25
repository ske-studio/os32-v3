#pragma once
#include "types.h"
static inline u16 ppc_load16(u32 va)
{
    u16 word;
    __asm__ volatile("lhz %0,0(%1)\n\teieio" : "=r"(word) : "b"(va) : "memory");
    return word;
}
static inline void ppc_store16(u32 va, u16 word)
{
    __asm__ volatile("sth %0,0(%1)\n\teieio" :: "r"(word), "b"(va) : "memory");
}
