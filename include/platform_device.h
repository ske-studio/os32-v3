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

#endif /* PLATFORM_DEVICE_H */
