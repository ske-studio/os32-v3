#ifndef PM_PRODUCT_BOOT_H
#define PM_PRODUCT_BOOT_H

#include "platform_handoff_layout.h"

#define PM_BOOT_MAGIC 0x4f533332
struct pm_boot_plan {
    unsigned boot_partition, root_partition, root_start, root_sectors, root_type;
};
struct pm_product_boot {
    unsigned magic, version, size, kind;
    struct pm_boot_plan plan;
    unsigned path_bytes;
    char path[256];
};
_Static_assert(sizeof(struct pm_product_boot) == PM_HANDOFF_BYTES, "handoff size");
extern struct pm_product_boot pm_product_handoff;
extern const volatile struct pm_boot_plan pm_expected_plan;
#endif
