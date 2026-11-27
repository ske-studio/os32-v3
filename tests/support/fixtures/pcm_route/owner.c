#include <stdio.h>
#include "sys/dev/dma/dma8237.h"

unsigned model_if = 1;
static unsigned writes;
void outp(unsigned port, unsigned value) { ++writes; }
unsigned inp(unsigned port) { return 0; }

void kprintf(u8 attr, const char *fmt, ...) {}

int main(void)
{
    char pcm, other;
    dma8237_init();
    writes = 0;
    if (dma_chan_acquire(1, &pcm)) return 1;
    dma_chan_mask(1, &other);
    dma_chan_unmask(1, &other);
    if (writes) {
        fprintf(stderr, "other owner mask\n");
        return 1;
    }
    dma_chan_mask(1, &pcm);
    if (!dma_chan_acquire(1, &other) ||
        !dma_chan_release(1, &other) || !dma_chan_quarantine(1, &other) ||
        dma_chan_release(1, &pcm) || dma_chan_acquire(1, &other) ||
        dma_chan_setup(1, &other, 0x10000, 512, DMA_DIR_TO_MEM, DMA_MODE_SINGLE) != DMA_ERR_STATE ||
        dma_chan_quarantine(1, &other) || !dma_chan_release(1, &other) ||
        !dma_chan_acquire(1, &pcm)) {
        fprintf(stderr, "DMA quarantine ownership\n");
        return 1;
    }
    return 0;
}
