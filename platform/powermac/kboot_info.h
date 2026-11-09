#ifndef OS32_POWERMAC_KBOOT_INFO_H
#define OS32_POWERMAC_KBOOT_INFO_H
#include "ofw.h"
#include "ppc_pmap.h"
/* Native platform-owned snapshot. Raw properties below retain BE encoding. */
#define PM_KBOOT_PROP_BYTES 512
#define PM_KBOOT_ANCESTORS 8
#define PM_KBOOT_TREE_LIMIT 1024
struct pm_kboot_property {
    unsigned present, bytes;
    unsigned char data[PM_KBOOT_PROP_BYTES];
};
struct pm_kboot_node {
    int node, parent;
    unsigned ac, sc;
    char path[PM_PATH_CAP];
    struct pm_kboot_property reg, assigned, ranges;
};
/* Leaf first, ending at root; ESCC leaf is ch-a when present. */
struct pm_kboot_device {
    unsigned count;
    struct pm_kboot_node v[PM_KBOOT_ANCESTORS];
};
struct pm_kboot_info {
    struct pm_regions ram, available;
    u32 admitted_end, image_first, image_end, hpt_pa, hpt_bytes;
    u32 tb_hz, escc_pa, mpic_pa, mpic_bytes;
    u32 of_calls_at_detach;
    struct pm_kboot_device escc, mpic;
};
const struct pm_kboot_info *pm_kboot_capture(ppc_ci ci);
void pm_kernel_takeover(const struct pm_kboot_info *) __attribute__((noreturn));
void ppc_kernel_main(void) __attribute__((noreturn));
#endif
