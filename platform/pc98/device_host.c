#include "platform_device.h"
#include "platform_host.h"
#include "np2sysp.h"
#include "link.h"

int platform_np2_detect(void)
{
    return np2_detect();
}

int platform_np2_get_version(char *buf, int maxlen)
{
    return np2_get_version(buf, maxlen);
}

int platform_np2_get_cpu(char *buf, int maxlen)
{
    return np2_get_cpu(buf, maxlen);
}

int platform_np2_get_clock(char *buf, int maxlen)
{
    return np2_get_clock(buf, maxlen);
}

int platform_np2_check_hostdrv(char *buf, int maxlen)
{
    return np2_check_hostdrv(buf, maxlen);
}

i32 platform_host_open(const char *req, u32 len, int owner)
{
    return link_host_open(req, len, owner);
}

i32 platform_host_status(i32 h, u32 *status, u32 *length, int owner)
{
    return link_host_status(h, status, length, owner);
}

i32 platform_host_read_stage(i32 h, u32 cap, const u8 **out, int owner)
{
    return link_host_read_stage(h, cap, out, owner);
}

i32 platform_host_write(i32 h, const void *buf, u32 len, int owner)
{
    return link_host_write(h, buf, len, owner);
}

i32 platform_host_close(i32 h, int owner)
{
    return link_host_close(h, owner);
}

void platform_host_owner_exit(int owner)
{
    link_host_owner_exit(owner);
}
