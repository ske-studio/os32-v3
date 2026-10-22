#ifndef HWDEV_OWNER_H
#define HWDEV_OWNER_H
#include "types.h"
/* v1 policy input, not a dynamic registry. Unknown is deliberately zero. */
enum hwdev_owner_state {
    HWDEV_UNCLASSIFIED, HWDEV_STOPPED_UNOWNED,
    HWDEV_ROOT_IDE, HWDEV_LAN_SERIALFS, HWDEV_DISPLAY
};
struct hwdev_device_owner {
    u32 bdf, platform_id, generation;
    u32 io_base, io_len, mmio_base, mmio_len, irq_mask, dma_mask;
    enum hwdev_owner_state state;
};
struct hwdev_lease {
    u32 generation, owner_generation, session;
    bool active;
};
static inline int hwdev_claim(struct hwdev_lease *lease,
                              const struct hwdev_device_owner *device, u32 session)
{
    if (lease->active || lease->generation == ~0U || !device->generation ||
        device->state != HWDEV_STOPPED_UNOWNED || device->dma_mask) return -1;
    lease->generation++;
    lease->owner_generation = device->generation;
    lease->session = session;
    lease->active = true;
    return 0;
}
static inline bool hwdev_lease_matches(const struct hwdev_lease *lease,
    const struct hwdev_device_owner *device, u32 generation, u32 session)
{
    return lease->active && lease->generation == generation &&
        lease->owner_generation == device->generation && lease->session == session &&
        device->state == HWDEV_STOPPED_UNOWNED;
}
#endif
