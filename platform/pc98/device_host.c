#include "platform_device.h"
#include "np2sysp.h"

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
