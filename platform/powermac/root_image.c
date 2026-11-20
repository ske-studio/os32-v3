#include "boot_root.h"
#include "bootinfo.h"
#include "crc32.h"
#include "endian_le.h"

extern const u8 __root_record_start[];

int pm_root_image(const struct boot_root_image *image)
{
    struct boot_common info = {0};
    if (image->base && image->bytes && image->bytes == le32_rd(__root_record_start + 12)) {
        u32 crc = crc32_calc(image->base, image->bytes);
        if (crc == le32_rd(__root_record_start + 4)) {
            info.image_valid = 1;
            info.image_crc = crc;
            info.image_size = image->bytes;
        }
    }
    boot_common_set(&info, BOOTINFO_SRC_NONE);
    return info.image_valid;
}
