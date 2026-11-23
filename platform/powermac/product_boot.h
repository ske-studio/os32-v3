#ifndef PM_PRODUCT_BOOT_H
#define PM_PRODUCT_BOOT_H

#define PM_HANDOFF_OFFSET 256
#define PM_HANDOFF_BYTES 296
#define PM_BOOT_MAGIC 0x4f533332
#if !defined(__ASSEMBLER__) && !defined(PM_PRODUCT_LINKER)
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
#endif
