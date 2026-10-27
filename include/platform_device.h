#ifndef PLATFORM_DEVICE_H
#define PLATFORM_DEVICE_H

#include "types.h"

/* Fixed synchronous device calls. State and fd ownership stay in loop_dev. */
int platform_loop_dev_get_geometry(int slot, u16 *c, u8 *h, u8 *s,
                                   u16 *bps, u32 *total);
int platform_loop_dev_read_chs(int slot, u16 c, u8 h, u8 s, void *buf);
int platform_loop_dev_read_lba(int slot, u32 lba, u32 count, void *buf);
int platform_loop_dev_attach(const char *path, int slot);
void platform_loop_dev_detach(int slot);
int platform_loop_dev_status(int slot, u32 *total, int *bps);

/* IDE state stays in the driver; KAPI range/authorization checks stay above. */
void platform_ide_init(void);
int platform_ide_drive_present(int drv);
int platform_ide_identify(int drv, void *info);
int platform_ide_read_sector(int drv, u32 lba, void *buf);
int platform_ide_write_sector(int drv, u32 lba, const void *buf);
int platform_ide_write_sectors(int drv, u32 lba, u32 cnt, const void *buf);
int platform_ide_get_info(int drv, void *info);

/* Value-only FS geometry; partition arithmetic remains in the caller. */
struct platform_disk_geometry {
    u32 total_sectors;
    u16 cylinders, heads, sectors, phys_sector_size;
};
int platform_ide_range_ok(int drv, u32 lba, u32 count);
int platform_disk_geometry_get(int drv, struct platform_disk_geometry *out);

#endif /* PLATFORM_DEVICE_H */
