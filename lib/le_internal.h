#ifndef OS32_LE_INTERNAL_H
#define OS32_LE_INTERNAL_H

#include "../include/types.h"

static inline u32 le_read32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static inline u16 le_read16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

static inline void le_write16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static inline void le_write32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

#endif
