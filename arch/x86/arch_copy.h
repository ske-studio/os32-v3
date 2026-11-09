#ifndef OS32_ARCH_COPY_H
#define OS32_ARCH_COPY_H

static inline void arch_copy16(void *dst, const void *src, unsigned int units) {
    __asm__ volatile("rep movsw"
                     : "+D"(dst), "+S"(src), "+c"(units)
                     : : "memory");
}

static inline void arch_copy32(void *dst, const void *src, unsigned int units) {
    __asm__ volatile("rep movsl"
                     : "+D"(dst), "+S"(src), "+c"(units)
                     : : "memory");
}

#endif
