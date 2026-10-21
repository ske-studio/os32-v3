/* The body only publishes immutable descriptors; M3b connects trial dispatch. */
#include "generated/hwdev_contract.h"
#include "hwdev_pair.h"

STATIC_ASSERT(__builtin_types_compatible_p(__typeof__(&gfinit_call),
              __typeof__(((struct hwdev_ops *)0)->call)), hwdev_call_type);
STATIC_ASSERT(__builtin_types_compatible_p(__typeof__(&gfinit_recover_end),
              __typeof__(((struct hwdev_ops *)0)->release_work)), hwdev_release_type);
static const struct hwdev_ops callbacks = {
    HWDEV_ABI, sizeof(struct hwdev_ops), gfinit_call, gfinit_recover_end
};
static const struct hwdev_manifest manifest = {
    {HWDEV_ABI, sizeof(struct hwdev_identity), HWDEV_KIND_GF6200, HWDEV_CONTRACT_CRC,
     RECOVER_PROFILE_VERSION, HWDEV_KERNEL_PAIR_CRC, HWDEV_CORE_IMAGE_CRC,
     HWDEV_CONTRACT_CRC},
    HWDEV_ABI, sizeof(struct hwdev_manifest), HWDEV_LAYOUT_CRC,
    HWDEV_LIFETIME_FLAGS, HWDEV_WORK_MAX_PAGES, 0, HWDEV_ABI,
    sizeof(struct hwdev_ops), 0
};
static int initialize(const struct hwdev_manifest **m, const struct hwdev_ops **ops)
{
    *m = &manifest;
    *ops = &callbacks;
    return 0;
}
struct hwdev_exports {
    u32 version;
    int (*initialize)(const struct hwdev_manifest **, const struct hwdev_ops **);
};
const struct hwdev_exports hwdev_ops = {HWDEV_ABI, initialize};
int hwdev_init(struct hwdev_exports *out)
{
    *out = hwdev_ops;
    return 0;
}
