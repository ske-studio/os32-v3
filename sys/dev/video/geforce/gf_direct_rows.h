#ifndef OS32_GF_DIRECT_ROWS_H
#define OS32_GF_DIRECT_ROWS_H

#include "types.h"

/* Shared with the foreground measurement on the off-screen WC view. */
static inline void gf_direct_write_row(volatile u8 *d, const u8 *s, u32 n)
{
    while (n && ((uptr)d & 3U)) { *d++ = *s++; --n; }
    while (n >= 4) {
        u32 value;
        __builtin_memcpy(&value, s, sizeof(value));
        *(volatile u32 *)d = value;
        s += 4;
        d += 4;
        n -= 4;
    }
    while (n) { *d++ = *s++; --n; }
}

static inline void gf_direct_read_row(u8 *d, const volatile u8 *s, u32 n)
{
    for (u32 x = 0; x < n; x++) d[x] = s[x];
}
#endif
