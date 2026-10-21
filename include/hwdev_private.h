#ifndef HWDEV_PRIVATE_H
#define HWDEV_PRIVATE_H
#include "types.h"
#include "kdebug/recover.h"
#define HWDEV_ABI 1U
#define HWDEV_KIND_GF6200 1U
#define HWDEV_LIFETIME_FLAGS 7U
#define HWDEV_WORK_MAX_PAGES 267U
struct hwdev_identity {
    u32 abi, size, kind, build_id, profile_version;
    u32 kernel_pair_crc, core_image_crc, contract_crc;
};
struct hwdev_ops {
    u32 abi, size;
    int (*call)(u32 op, void *copied_req, u32 size,
                const struct recover_ops *base,
                struct recover_transport *transport,
                const struct recover_profile *s0);
    void (*release_work)(void);
};
struct hwdev_manifest {
    struct hwdev_identity identity;
    u32 version, size, layout_crc, lifetime_flags;
    u32 work_max_pages, dma_max_pages, ops_abi, ops_size, reserved;
};
#endif
