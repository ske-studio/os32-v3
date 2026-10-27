#include "platform_device.h"
#include "loop_dev.h"

int platform_loop_dev_get_geometry(int slot, u16 *c, u8 *h, u8 *s,
                                   u16 *bps, u32 *total)
{
    return loop_dev_get_geometry(slot, c, h, s, bps, total);
}

int platform_loop_dev_read_chs(int slot, u16 c, u8 h, u8 s, void *buf)
{
    return loop_dev_read_chs(slot, c, h, s, buf);
}

int platform_loop_dev_read_lba(int slot, u32 lba, u32 count, void *buf)
{
    return loop_dev_read_lba(slot, lba, count, buf);
}

int platform_loop_dev_attach(const char *path, int slot)
{
    return loop_dev_attach(path, slot);
}

void platform_loop_dev_detach(int slot)
{
    loop_dev_detach(slot);
}

int platform_loop_dev_status(int slot, u32 *total, int *bps)
{
    return loop_dev_status(slot, total, bps);
}
