/* MPC7450UM 3.4.4: publish writes before fetching instructions. */
#include "exec_cache.h"
#define __ASSEMBLER__
#include "ppc_oea.h"
#undef __ASSEMBLER__

typedef __UINTPTR_TYPE__ cache_addr_t;

void arch_exec_publish(void *dst, u32 bytes)
{
    cache_addr_t start = (cache_addr_t)dst;
    cache_addr_t first = start & ~(cache_addr_t)(PPC_CACHE_LINE - 1);
    cache_addr_t last = (start + bytes - 1) & ~(cache_addr_t)(PPC_CACHE_LINE - 1);
    cache_addr_t line;

    for (line = first;; line += PPC_CACHE_LINE) {
        __asm__ volatile("dcbst 0,%0" : : "r"(line) : "memory");
        if (line == last) break;
    }
    __asm__ volatile("sync" : : : "memory");
    for (line = first;; line += PPC_CACHE_LINE) {
        __asm__ volatile("icbi 0,%0" : : "r"(line) : "memory");
        if (line == last) break;
    }
    __asm__ volatile("sync; isync" : : : "memory");
}
