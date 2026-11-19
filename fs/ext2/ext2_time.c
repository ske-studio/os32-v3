#include "ext2_priv.h"

#define EXT2_FALLBACK_EPOCH 0x67E8E800UL /* 2025-03-30 06:43:12 UTC */

u32 ext2_current_time(void)
{
    return EXT2_FALLBACK_EPOCH;
}
