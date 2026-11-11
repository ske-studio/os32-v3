#include "boot_root.h"

extern const u8 __root_image_start[], __root_image_end[];

void pm_root_image(struct boot_root_image *out)
{
    out->base = __root_image_start;
    out->bytes = (u32)(__root_image_end - __root_image_start);
}
