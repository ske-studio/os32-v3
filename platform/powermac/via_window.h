#ifndef OS32_POWERMAC_VIA_WINDOW_H
#define OS32_POWERMAC_VIA_WINDOW_H
#include "types.h"

#define PM_VIA_VA    0xfe3c4000U
#define PM_VIA_BYTES 0x3000U
#define PM_VIA_REG_BYTES 0x1c01U

enum pm_via_window_rc {
    PM_VIA_NOT_READY = -10, PM_VIA_BAD_PLAN = -11,
    PM_VIA_OVERLAP = -12, PM_VIA_BAD_WALK = -13
};
struct pm_via_window { u32 va; int rc; };
const struct pm_via_window *pm_via_window(void);
void pm_via_prepare(void);
#endif
