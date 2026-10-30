#ifndef OS32_LE_INTERNAL_H
#define OS32_LE_INTERNAL_H

#include "types.h"

static inline u32 le_read32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static inline u32 le_read_n(const u8 *p, u32 n)
{
    u32 v = 0;
    for (u32 i = 0; i < n; i++) v |= (u32)p[i] << (8 * i);
    return v;
}

#endif
