#ifndef OS32_POWERMAC_KBOOT_INFO_H
#define OS32_POWERMAC_KBOOT_INFO_H
#include "ofw.h"
#include "lan_boot.h"
#include "ppc_pmap.h"
#include "ppc_trap.h"   /* PM_DEC_HZ */
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
enum pm_via_kind { PM_VIA_NONE, PM_VIA_PMU, PM_VIA_CUDA };
enum pm_via_capture_rc {
    PM_VIA_MISSING = -20, PM_VIA_AMBIGUOUS = -21,
    PM_VIA_BAD_PROPERTY = -22, PM_VIA_BAD_ADDRESS = -23
};
struct pm_via_boot {
    u8 kind;
    u32 pa, bytes;
    int capture_rc;
    char compatible[PM_KBOOT_PROP_BYTES + 1];
};
void pm_via_capture(int root, struct pm_via_boot *out);

struct pm_kboot_info {
    struct pm_regions ram, available;
    u32 admitted_end, image_first, image_end, hpt_pa, hpt_bytes;
    u32 tb_hz, escc_pa, mpic_pa, mpic_bytes;
    u32 of_calls_at_detach;
    struct pm_kboot_device escc, mpic;
    struct pm_gmac_boot gmac;
    int gmac_rc;
    struct pm_via_boot via;
};
/* Boot foreground, EE=0: copy CPU TB frequency before clock init. */
void pm_clock_bind(const struct pm_kboot_info *info);
struct io_clock_ops;
extern const struct io_clock_ops pm_io_clock_ops;
void pm_io_clock_install(void);
u32 pm_tick_hz(void); /* Physical DEC Hz. */
const struct pm_kboot_info *pm_kboot_capture(ppc_ci ci);
/* Final snapshot only: after reserve, no OF allocation/map before detach. */
void pm_kboot_refresh_available(struct pm_kboot_info *info);
enum pm_kboot_reserve_result {
    PM_KBOOT_RESERVE_OK, PM_KBOOT_RESERVE_RANGES, PM_KBOOT_RESERVE_IMAGE,
    PM_KBOOT_RESERVE_IMAGE_AVAILABLE, PM_KBOOT_RESERVE_VECTOR,
    PM_KBOOT_RESERVE_WINDOW, PM_KBOOT_RESERVE_HPT
};
int pm_kboot_reserve(struct pm_kboot_info *info);
void pm_kernel_takeover(const struct pm_kboot_info *) __attribute__((noreturn));
#endif
