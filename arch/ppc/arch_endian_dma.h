#ifndef PPC_ARCH_ENDIAN_DMA_H
#define PPC_ARCH_ENDIAN_DMA_H
/* CPU instruction precedent: platform/powermac/lan_gem_io.c:13-24.
 * USB_TEST_SWAP emulates a BE native load/store on an LE test host, then
 * executes the same byte reversal as lwbrx/stwbrx. No cache policy here. */
#ifdef USB_TEST_SWAP
static inline unsigned int usb_test_native_load(const volatile void *p)
{
    const volatile u8 *b = p;
    return ((unsigned int)b[0] << 24) | ((unsigned int)b[1] << 16) |
           ((unsigned int)b[2] << 8) | b[3];
}
static inline u32 mmio_rd32_le(const volatile void *p)
{
    return __builtin_bswap32(usb_test_native_load(p));
}
static inline void mmio_wr32_le(volatile void *p, u32 v)
{
    unsigned int n = __builtin_bswap32((unsigned int)v);
    volatile u8 *b = p;
    b[0] = (u8)(n >> 24); b[1] = (u8)(n >> 16);
    b[2] = (u8)(n >> 8); b[3] = (u8)n;
}
#else
static inline u32 mmio_rd32_le(const volatile void *p)
{
    u32 v;
    __asm__ volatile("lwbrx %0,0,%1; eieio" : "=r"(v) : "r"(p) : "memory");
    return v;
}
static inline void mmio_wr32_le(volatile void *p, u32 v)
{
    __asm__ volatile("sync; stwbrx %0,0,%1; eieio" : : "r"(v), "r"(p) : "memory");
}
#endif
static inline u32 dma_le32_get(const volatile le32_t *p)
{
    return mmio_rd32_le(&p->raw);
}
static inline void dma_le32_put(volatile le32_t *p, u32 v)
{
    mmio_wr32_le(&p->raw, v);
}
#endif
