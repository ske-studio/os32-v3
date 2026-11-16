#ifndef IO_POLICY_H
#define IO_POLICY_H

#include "types.h"

/* IO_TIER §4/§6: permanent service IDs (not registration indexes). */
enum {
    IO_SVC_NE2K_BH = 1,
    IO_SVC_NET_RX = 2,
    IO_SVC_TSC_CALIBRATE = 3,
    IO_SVC_LAN_BULK = 4
};

/* Active value for a known ID: tier 1..3 and budget in us.
 * 0 on success, OS32_ERR_NOTFOUND for an unknown ID. */
int io_policy_lookup(u32 id, u8 *tier, u32 *budget_us);
/* Default and allowed range from the constant table (registration check). */
int io_policy_limits(u32 id, u8 *default_tier, u32 *default_budget_us,
                     u32 *budget_min_us, u32 *budget_max_us);
/* Bumped on every successful set/reset/restore; the dispatcher refreshes
 * its cached tiers when it changes. */
u32 io_policy_generation(void);

/* FG1 only. owner = res_owner_get() app ID, gen = that ID's generation.
 * owner 0 is the boot/user permanent table (no save/restore). */
int io_policy_set(const void *bytes, unsigned int len, u32 owner, u32 gen);
int io_policy_get(void *bytes, unsigned int cap, unsigned int *needed);
int io_policy_reset(u32 owner, u32 gen);
/* Common reclaim (normal exit, fault, kill): restore or relink the table
 * saved when this owner most recently became the newest writer.
 * Reapplying below a newer owner refreshes that saved source.
 * Stale gen is ignored. */
void io_policy_owner_exit(u32 owner, u32 gen);

/* Provided by the dispatcher (io_work.c): nonzero while io_dispatch runs,
 * so set/reset return OS32_ERR_BUSY instead of swapping. owner_exit swaps
 * anyway: the dispatcher re-reads tiers only at round entry (design P3). */
int io_dispatch_running(void);

#endif
