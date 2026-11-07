#ifndef BOOTINFO_H
#define BOOTINFO_H
#include "types.h"
#define BOOT_BUILD_TEXT_MAX 32
#define BOOTINFO_SRC_NONE 0U
#define BOOTINFO_SRC_FD 1U
#define BOOTINFO_SRC_HDD 2U
struct boot_common { int image_valid; u32 image_crc, image_size; };
const struct boot_common *boot_common_get(void);
/* Provider publishes native image information after validating its input. */
void boot_common_set(const struct boot_common *info, u8 source);
int boot_image_info(void *out);
void bootinfo_report_image(void);
void bootinfo_report(void);
/* Disk geometry compatibility facade; the platform owns BIOS details. */
#define BOOTINFO_GEOM_BIOS 1
#define BOOTINFO_GEOM_IDENTIFY 2
int bootinfo_part_geom(int drive, u16 *heads, u16 *spt);
int hdd_geom_info(int drive, void *out);
#endif
