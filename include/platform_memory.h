/* Internal, link-time platform memory contract. All spans are half-open. */
#ifndef KERNEL_PLATFORM_MEMORY_H
#define KERNEL_PLATFORM_MEMORY_H

#include "types.h"

struct platform_memory_geometry {
    u32 legacy_end, system_first, system_end, high_first, ram_ceiling;
};

#define PLATFORM_MEMORY_NATIVE_UC_COUNT 3
struct platform_memory_span { u32 first, end; };
struct platform_memory_attributes {
    struct platform_memory_span native_uc[PLATFORM_MEMORY_NATIVE_UC_COUNT];
    struct platform_memory_span bios_rom;
    struct platform_memory_span low_device; /* Ledger FIXED/UC, not PTE policy. */
};

/* Confirmed high RAM end PFN; zero when no high RAM was confirmed.
 * Called once before paging, with IF=0. */
u32 platform_memory_probe_high(void);

#include "../platform/pc98/platform_memory_desc.h"

#endif /* KERNEL_PLATFORM_MEMORY_H */
