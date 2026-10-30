#include "boot_platform.h"

/* Registered once before IRQ publication; the platform table stays resident. */
static const struct boot_platform *boot_ops;

void boot_platform_register(const struct boot_platform *ops)
{
    boot_ops = ops;
}

void boot_platform_run(enum boot_point point, const struct boot_context *ctx)
{
    boot_ops->step[point](ctx);
}
