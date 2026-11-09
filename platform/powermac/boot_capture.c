#include "platform_boot.h"
#include "bootinfo.h"
#include "kboot_layout.h"
/* OF snapshot/reservations stay platform-owned. No CRC has been verified. */
void platform_boot_capture(void)
{
    const struct boot_common image = {
        0, 0, (u32)__kernel_file_end - (u32)__kernel_image_start
    };
    boot_common_set(&image, BOOTINFO_SRC_NONE);
}
