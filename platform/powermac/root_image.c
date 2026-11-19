#include "boot_root.h"
#include "bootinfo.h"
#include "crc32.h"
#include "root_image_expected.h"

extern const u8 __root_image_start[], __root_image_end[];

void pm_root_image(struct boot_root_image *out)
{
    out->base = __root_image_start;
    out->bytes = (u32)(__root_image_end - __root_image_start);

    struct boot_common info = {0};
    if (out->bytes && out->bytes == PM_ROOT_IMAGE_BYTES) {
        u32 crc = crc32_calc(out->base, out->bytes);
        if (crc == PM_ROOT_IMAGE_CRC) {
            info.image_valid = 1;
            info.image_crc = crc;
            info.image_size = out->bytes;
        }
    }
    boot_common_set(&info, BOOTINFO_SRC_NONE);
}
