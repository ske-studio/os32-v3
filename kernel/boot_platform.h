#ifndef BOOT_PLATFORM_H
#define BOOT_PLATFORM_H
#include "types.h"
/* Internal boot boundaries; kernel_main invokes each at its original position. */
enum boot_point {
    BOOT_DMA,
    BOOT_INPUT,
    BOOT_FLOPPY,
    BOOT_DEV_FS,
    BOOT_PCI_ENUM,
    BOOT_DISK,
    BOOT_AUTO_MOUNT,
    BOOT_CLOCK,
    BOOT_FONT,
    BOOT_PCI_RESOURCES,
    BOOT_NIC_LEGACY,
    BOOT_POINT_COUNT
};
struct boot_context { u32 drive; const char *root_dev, *root_fs; };
typedef void (*boot_step_fn)(const struct boot_context *);
struct boot_platform { boot_step_fn step[BOOT_POINT_COUNT]; };
void boot_platform_register(const struct boot_platform *ops);
void boot_platform_run(enum boot_point point, const struct boot_context *ctx);
#endif
