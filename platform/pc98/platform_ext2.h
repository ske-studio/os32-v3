#ifndef PLATFORM_EXT2_H
#define PLATFORM_EXT2_H

#include "dev.h"

/* On success, base and len are measured in sectors of dev. */
int platform_ext2_resolve(int logical_drive, Device **dev, u32 *base, u32 *len);

#endif
