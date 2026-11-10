#include <stdio.h>
#include <stdint.h>
#include "drivers/net/gem/gem.h"
int ppc_dma_sync(void *, u32, enum dma_sync);
static char events[128];
static unsigned n;
static void event(char c, void *p) { n += (unsigned)snprintf(events+n, sizeof(events)-n, "%c%lx ", c, (unsigned long)p); }
void ppc_dcbst_line(void *p) { event('w', p); }
void ppc_dcbf_line(void *p) { event('f', p); }
void ppc_dcbi_line(void *p) { event('i', p); }
void ppc_sync(void) { event('s', 0); }
static int check(const char *name, uintptr_t p, u32 bytes, enum dma_sync op, int rc, const char *want)
{
    n = 0; events[0] = 0;
    int got = ppc_dma_sync((void *)p, bytes, op);
    if (got != rc || __builtin_strcmp(events, want)) {
        printf("%s\n", name); return 1;
    }
    return 0;
}
int main(void)
{
    puts("REACHED host");
    if (check("H:A4 dma-unpublished", 0x1001, 64, DMA_PREWRITE, 0, "w1000 w1020 w1040 s0 ")) return 1;
    if (check("H:preread", 0x1001, 32, DMA_PREREAD, 0, "f1000 f1020 s0 ")) return 1;
    if (check("H:postread", 0x1000, 64, DMA_POSTREAD, 0, "i1000 i1020 s0 ")) return 1;
    if (check("H:postwrite", 0x1001, 1, DMA_POSTWRITE, 0, "s0 ")) return 1;
    if (check("H:boundary", 0x1001, 31, DMA_POSTREAD, GEM_EINVAL, "")) return 1;
    if (check("H:boundary", 0x1000, 31, DMA_POSTREAD, GEM_EINVAL, "")) return 1;
    if (check("H:zero", 0x1000, 0, DMA_PREWRITE, GEM_EINVAL, "")) return 1;
    if (check("H:overflow", UINTPTR_MAX-15, 32, DMA_PREWRITE, GEM_EINVAL, "")) return 1;
    if (check("H:op", 0x1000, 32, (enum dma_sync)99, GEM_EINVAL, "")) return 1;
    return 0;
}
