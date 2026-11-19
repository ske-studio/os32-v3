#include "platform_device.h"
#include "kcg_contract.h"

void platform_kcg_init(void)
{
    kcg_init();
}

void platform_kcg_set_scale(int scale)
{
    kcg_set_scale(scale);
}

int platform_kcg_load_font(const char *path)
{
    return kcg_load_font(path);
}
