/* MPC7450UM 3.4.4; OpenBSD gem.c:977-988,1082 DMA ownership sync. */
#include "drivers/net/gem/gem.h"
#define __ASSEMBLER__
#include "ppc_oea.h"
#undef __ASSEMBLER__

typedef __UINTPTR_TYPE__ dma_addr_t;

void ppc_dcbst_line(void *line);
void ppc_dcbf_line(void *line);
void ppc_dcbi_line(void *line);
void ppc_sync(void);
int ppc_dma_sync(void *cpu, u32 bytes, enum dma_sync op);

#ifndef PPC_DMA_TEST
void ppc_dcbst_line(void *line) { __asm__ volatile("dcbst 0,%0" : : "r"(line) : "memory"); }
void ppc_dcbf_line(void *line) { __asm__ volatile("dcbf 0,%0" : : "r"(line) : "memory"); }
void ppc_dcbi_line(void *line) { __asm__ volatile("dcbi 0,%0" : : "r"(line) : "memory"); }
void ppc_sync(void) { __asm__ volatile("sync" : : : "memory"); }
#endif

int ppc_dma_sync(void *cpu, u32 bytes, enum dma_sync op)
{
    dma_addr_t start = (dma_addr_t)cpu;
    dma_addr_t end, line, last;

    if (!bytes || (dma_addr_t)bytes > ~(dma_addr_t)0 - start ||
        (op != DMA_PREWRITE && op != DMA_PREREAD &&
         op != DMA_POSTREAD && op != DMA_POSTWRITE))
        return GEM_EINVAL;
    end = start + bytes;
    if (op == DMA_POSTREAD && ((start | end) & (PPC_CACHE_LINE - 1)))
        return GEM_EINVAL;
    line = start & ~(dma_addr_t)(PPC_CACHE_LINE - 1);
    last = (end - 1) & ~(dma_addr_t)(PPC_CACHE_LINE - 1);
    if (op != DMA_POSTWRITE) {
        for (;;) {
            if (op == DMA_PREWRITE)
                ppc_dcbst_line((void *)line);
            else if (op == DMA_PREREAD)
                ppc_dcbf_line((void *)line);
            else
                ppc_dcbi_line((void *)line);
            if (line == last)
                break;
            line += PPC_CACHE_LINE;
        }
    }
    ppc_sync();
    return GEM_OK;
}
