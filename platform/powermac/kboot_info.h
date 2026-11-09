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
/* KeyLargo family: NetBSD macppc/dev/obio.c:123-126,458.
 * Identity selects later accessors; CPU byte order is not an MMIO rule.
 * KeyLargo MPIC uses LE (NetBSD powerpc/include/openpic.h:41-53);
 * ESCC uses byte registers (macppc/dev/zs.c:886-890,914-919). */
enum pm_kboot_machine { PM_KBOOT_MACHINE_UNKNOWN, PM_KBOOT_MACHINE_KEYLARGO };
struct pm_kboot_node {
    int node, parent;
    unsigned ac, sc;
    char path[PM_PATH_CAP];
    struct pm_kboot_property reg, assigned, ranges, compatible;
};
/* Leaf first, ending at root; ESCC leaf is ch-a when present. */
struct pm_kboot_device {
    unsigned count;
    enum pm_kboot_machine machine;
    struct pm_kboot_node v[PM_KBOOT_ANCESTORS];
};
struct pm_kboot_info {
    struct pm_regions ram, available;
    u32 admitted_end, image_first, image_end, hpt_pa, hpt_bytes;
    u32 tb_hz, escc_pa, mpic_pa, mpic_bytes;
    u32 of_calls_at_detach;
    struct pm_kboot_device escc, mpic;
};
/* Boot foreground, EE=0: copy CPU TB frequency before clock init. */
void pm_clock_bind(const struct pm_kboot_info *info);
u32 pm_tick_hz(void);
const struct pm_kboot_info *pm_kboot_capture(ppc_ci ci);
void pm_kernel_takeover(const struct pm_kboot_info *) __attribute__((noreturn));
void ppc_kernel_main(void) __attribute__((noreturn));
#endif
