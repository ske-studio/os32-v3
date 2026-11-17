#ifndef X86_ARCH_ENDIAN_DMA_H
#define X86_ARCH_ENDIAN_DMA_H
#ifdef USB_TEST_SWAP
#include "../ppc/arch_endian_dma.h"
#else
static inline u32 dma_le32_get(const volatile le32_t *p)
{
    return p->raw;
}
static inline void dma_le32_put(volatile le32_t *p, u32 v)
{
    p->raw = (unsigned int)v;
}
static inline u32 mmio_rd32_le(const volatile void *p)
{
    u32 v = *(const volatile unsigned int *)p;
    __asm__ volatile("" ::: "memory");
    return v;
}
static inline void mmio_wr32_le(volatile void *p, u32 v)
{
    __asm__ volatile("" ::: "memory");
    *(volatile unsigned int *)p = (unsigned int)v;
}
#endif /* USB_TEST_SWAP */
#endif
