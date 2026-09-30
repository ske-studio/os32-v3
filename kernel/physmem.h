/* Physical page eligibility model, not a detector or an allocation bitmap.
 * Single-owner boot-time use: no locks, heap, static bitmap, hardware I/O,
 * paging changes, live-allocation state or byte-address exclusive endpoints.
 * All pointers must be valid. Initialize before use; fields are read-only to
 * callers. Iterate count/ranges for allocator eligibility; RAM means eligible,
 * NOT currently free. Count is a union count, not a physical-address high-water
 * mark or a detected RAM total. Non-RAM entries have sources == 0.
 * Each mutator commits a normalized full-space map only on success. A failed
 * exclusion MUST abort the caller's operation, never enable a device anyway.
 *
 * Device policy belongs at activation, not in this generic model. Inactive
 * apertures do not impose unconditional 15-18MiB CUI exclusions. Real holes
 * must still be excluded. Future activation must reject conflicts with live
 * allocations AND the entire legacy exec arena before enabling an aperture;
 * this model alone cannot detect those conflicts. Startup/splash ordering
 * needs a separate audit. RAM exclusion does not prohibit explicit device
 * mappings through paging. See synthetic aperture tests for existing PEGC
 * 0x00F00000/512KiB and Xe10 0x01000000/2MiB constants; an optional 1MiB
 * guard below 16MiB never replaces the separate full Xe10 aperture.
 */
#ifndef OS32_PHYSMEM_H
#define OS32_PHYSMEM_H
#include "types.h"

#define PHYSMEM_PAGE_SHIFT 12
#define PHYSMEM_PAGE_SIZE (1UL << PHYSMEM_PAGE_SHIFT)
#define PHYSMEM_MAX_PFN 1048576UL
#define PHYSMEM_MAX_RANGES 64
#define PHYSMEM_UNKNOWN 0UL
#define PHYSMEM_RAM 1UL
#define PHYSMEM_RESERVED 2UL
#define PHYSMEM_MMIO 3UL

struct physmem_range {
    u32 first, end; /* half-open PFNs; end may be 1048576 (4GiB) */
    u32 kind, sources;
};
struct physmem {
    u32 count;
    u32 legacy_ceiling; /* PFN bound, never extended by trusted high RAM */
    struct physmem_range ranges[PHYSMEM_MAX_RANGES];
};
/* Initialize a NEW boot-time model. Never reset a live allocator's model. */
void physmem_init(struct physmem *m);
#define PHYSMEM_SOURCE_LEGACY 1UL
#define PHYSMEM_SOURCE_MACHINE 2UL
#define PHYSMEM_SOURCE_SYNTHETIC 4UL
/* Caller attests to verified page-aligned RAM, not a size hint. MACHINE is the
 * authoritative machine source — the boot detector in kernel/memory_boot.c,
 * which reads the PC-98 BIOS work area and re-verifies each megabyte before
 * attesting to RAM above MEM_HIGH_RAM_BASE; SYNTHETIC is tests only.
 * SYNTHETIC requires physmem.c compiled with PHYSMEM_HOST_TEST=1 and without
 * __KERNEL_BUILD__. Kernel builds ALWAYS reject it, even with the host flag;
 * conflicting flags compile normally but cannot enable synthetic RAM.
 * Default builds reject it too. Pass one source, never a bitwise source mix.
 * The host flag must not be defined by a public header or production build.
 * No hardware verification is performed here. Return 1 success, 0 failure.
 * All ranges are nonempty half-open PFNs. Failure leaves model/output intact. */
/* Permanent boot-time exclusion: RESERVED or MMIO only, never releasable.
 * MMIO > RESERVED > RAM > implicit UNKNOWN, independent of insertion order.
 * This is not live allocation collision detection; callers must coordinate
 * reservations with pgalloc/legacy exec BEFORE enabling a device aperture. */
int physmem_exclude(struct physmem *m, u32 first, u32 end, u32 kind);
int physmem_add_trusted(struct physmem *m, u32 first, u32 end, u32 source);
int physmem_count(const struct physmem *m, u32 first, u32 end,
                  u32 kind, u32 *pages);
/* Old loader compatibility policy only: clamp mem_kb BEFORE arithmetic to the
 * most the 512KiB write probe can report (16MiB); reserve below the allocator
 * band. Does not detect RAM or import any reported memory above 16MiB, and is
 * NOT a ceiling on physical RAM (K6-RAM, 2026-09-11): the only ceiling is
 * PHYSMEM_MAX_PFN. RAM above MEM_HIGH_RAM_BASE is attested separately with
 * PHYSMEM_SOURCE_MACHINE and bounds neither the legacy arena nor this value.
 * No inactive-device aperture exclusions; the 15-16MiB PC-98 system space and
 * the top-of-4GiB ROM/MMIO band are excluded by the caller (memory_boot).
 * Other fixed reservations must be supplied by caller. */
#define PHYSMEM_LEGACY_MAX_PFN 4096UL
void physmem_bootstrap_legacy(struct physmem *m, u32 mem_kb);
/* Contiguous legacy exec end PFN (0 if unavailable), clipped at first hole.
 * Not the highest RAM address; convert to bytes only after this bounded query.
 * Caller must reserve/coordinate the whole live exec arena in pgalloc. */
u32 physmem_legacy_end(const struct physmem *m);
/* First-fit eligible RAM only; alignment is a nonzero power of two in pages.
 * No fallback outside [first,end). PFN 0 is valid; success is separate.
 * Does not consult live allocations or mappings. Boot-time metadata callers
 * must choose a window outside the entire legacy exec arena and other users. */
int physmem_find(const struct physmem *m, u32 first, u32 end,
                 u32 pages, u32 align_pages, u32 *pfn);
/* Permanently reserve a wholly eligible span (e.g. allocator metadata).
 * Unknown/reserved/MMIO overlap or capacity failure leaves model unchanged.
 * There is deliberately no release API. Not a live allocation operation. */
int physmem_reserve_ram(struct physmem *m, u32 first, u32 end);
#endif
