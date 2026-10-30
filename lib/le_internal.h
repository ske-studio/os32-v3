#ifndef OS32_LE_INTERNAL_H
#define OS32_LE_INTERNAL_H

#include "types.h"

static inline u32 le_read32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

#endif
