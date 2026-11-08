/* Opaque kernel jump context. exec_longjmp restores the saved IRQ/exception
 * depths and always lands with 1. It does not restore IF; the landing path
 * must restore interrupts. Keep the existing volatile rules at setjmp sites. */
#ifndef __KSETJMP_H
#define __KSETJMP_H

#include "types.h"
#include "arch_jmp_storage.h"

typedef struct { unsigned char opaque[ARCH_JMP_BYTES]; }
    __attribute__((aligned(ARCH_JMP_ALIGN))) k_jmpbuf[1];

int exec_setjmp(k_jmpbuf buf);
void exec_longjmp(k_jmpbuf buf);

/* Copy the owned context before freeing its slot. No native layout escapes. */
static inline void k_jmp_copy(k_jmpbuf dst, const k_jmpbuf src)
{
    *dst = *src;
}

#endif /* __KSETJMP_H */
