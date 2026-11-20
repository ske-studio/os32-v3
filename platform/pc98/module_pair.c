#include "platform_module_pair.h"
#include "bootinfo.h"

int platform_module_pair(u32 *out)
{
    const struct boot_common *boot = boot_common_get();
    if (!boot->image_valid) return 0;
    *out = boot->image_crc;
    return 1;
}
