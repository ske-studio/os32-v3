#ifndef OS32_INSTALL_MEDIUM_H
#define OS32_INSTALL_MEDIUM_H

#include "os32_kapi_shared.h"

/* The backend derives the changed sector list from the actual table and plan.
 * read/write/sync bypass public dispatch and address the reserved raw unit. */
struct _Device;
struct install_identity {
    u32 serial_crc, capacity, sector_bytes, media_generation;
};
struct install_medium {
    int (*identify)(u32 unit, struct install_identity *out);
    /* Re-identify the medium (including its serial and sector size), read the
     * entire old table, check the plan against its entries and preserved
     * extents. retry permits format residue only on the same identified medium.
     * Return the exact number of table sectors that may change. No writes. */
    int (*validate)(const InstallPlanV1 *, int retry, u32 *changed_count);
    int (*changed_lba)(const InstallPlanV1 *, u32 index, u32 *lba);
    /* Compare every proposed table byte with the approved generated table. */
    int (*check_table)(const InstallPlanV1 *, u32 lba, u32 count, const void *bytes);
    /* Resolve the current logical HD view to its raw unit. */
    int (*view_unit)(unsigned logical, u32 *unit);
    /* 0 = unrelated device, 1 = mapped, negative = stale or out of range.
     * A raw alias has generation 0. Views return their issuing generation. */
    int (*resolve)(struct _Device *, u32 lba, u32 count, u32 *unit,
                   u32 *physical, u32 *generation);
    int (*read)(u32 unit, u32 lba, void *sector);
    int (*write)(u32 unit, u32 lba, u32 count, const void *sectors);
    /* Wait for issued transfers and flush old dirty buffers with their saved
     * admission records. Return BUSY when an active reference cannot drain. */
    int (*drain)(u32 unit);
    int (*sync)(u32 unit);
    /* Read back every entry, including preserved entries. Bind exactly one
     * owner-only hd0 view for this generation; repeated calls are idempotent. */
    int (*rescan)(const InstallPlanV1 *, u32 generation);
    /* Recheck the final table and ext2 after sync and checked detach. */
    int (*verify)(const InstallPlanV1 *);
    void (*invalidate)(u32 unit);
    /* Publish the already verified view. IRQs are masked; no I/O or waits. */
    void (*publish)(u32 unit);
    int requires_raw_reservation;
    int (*is_view)(struct _Device *);
};

#endif
