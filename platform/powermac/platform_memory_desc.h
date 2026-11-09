/* PPC_KBOOT_DESIGN §4 R1: B2 proves [1,15)MiB; discard [15,16)MiB.
 * The HPT is outside managed RAM. Compatibility reservations below 1MiB
 * do not describe actual PowerMac ROM or devices. */
#ifndef POWERMAC_PLATFORM_MEMORY_DESC_H
#define POWERMAC_PLATFORM_MEMORY_DESC_H
#define PM_MEMORY_LEGACY_END 0x00f00000UL
#define PM_MEMORY_ADMITTED_END 0x01000000UL
#define PM_MEMORY_COMPAT_FIRST 0x000a0000UL
#define PM_MEMORY_COMPAT_END 0x00100000UL
static const struct platform_memory_geometry platform_memory_geometry = {
    PM_MEMORY_LEGACY_END, PM_MEMORY_LEGACY_END, PM_MEMORY_ADMITTED_END,
    PM_MEMORY_ADMITTED_END, PM_MEMORY_ADMITTED_END
};
static const struct platform_memory_attributes platform_memory_attributes = {
    { {0, 0}, {0, 0}, {0, 0} },
    {PM_MEMORY_COMPAT_FIRST, PM_MEMORY_COMPAT_END},
    {PM_MEMORY_COMPAT_FIRST, PM_MEMORY_COMPAT_END}
};
#endif /* POWERMAC_PLATFORM_MEMORY_DESC_H */
