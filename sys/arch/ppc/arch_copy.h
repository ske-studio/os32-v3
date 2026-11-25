#ifndef OS32_ARCH_COPY_H
#define OS32_ARCH_COPY_H

/* Byte accesses preserve order and allow unaligned buffers, including MMIO.
 * Keep unit stepping explicit, without an overflowing byte-count product. */
static inline void arch_copy16(void *dst, const void *src, unsigned int units) {
    volatile unsigned char *d = dst;
    const volatile unsigned char *s = src;
    while (units--) {
        d[0] = s[0]; d[1] = s[1];
        d += 2; s += 2;
    }
}

static inline void arch_copy32(void *dst, const void *src, unsigned int units) {
    volatile unsigned char *d = dst;
    const volatile unsigned char *s = src;
    while (units--) {
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        d += 4; s += 4;
    }
}
#endif
