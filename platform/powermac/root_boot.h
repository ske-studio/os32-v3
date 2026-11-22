#ifndef POWERMAC_ROOT_BOOT_H
#define POWERMAC_ROOT_BOOT_H

#include "boot_root.h"

int pm_root_release(const struct boot_root_image *image, int registered,
                    u32 frame_pfn, u32 pages, u32 *before, u32 *after);
int pm_root_boot(const struct boot_root_image *image, int *registered);

#endif
