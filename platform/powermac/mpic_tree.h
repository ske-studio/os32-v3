#ifndef OS32_POWERMAC_MPIC_TREE_H
#define OS32_POWERMAC_MPIC_TREE_H
#include "kboot_info.h"
enum pm_mpic_order { PM_MPIC_MMIO_LE, PM_MPIC_MMIO_BE };
enum pm_mpic_result {
    PM_MPIC_OK, PM_MPIC_BAD_TREE, PM_MPIC_BAD_PROPERTY,
    PM_MPIC_BAD_ADDRESS, PM_MPIC_NO_RANGE, PM_MPIC_BAD_ORDER
};
/* Pure snapshot decoder. Outputs change only on success; caller stores the
 * returned window in kboot_info. Unknown MMIO identities fail closed. */
int pm_mpic_resolve(const struct pm_kboot_info *, u32 *, u32 *, int *);
#endif
