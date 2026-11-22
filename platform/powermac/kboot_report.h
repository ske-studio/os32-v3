#ifndef PM_KBOOT_REPORT_H
#define PM_KBOOT_REPORT_H

#include "types.h"

struct pm_kboot_info;

/* Report successful self-checks in diagnostic images. Failures always print. */
extern const int pm_report_successes;

void pm_report_handoff(const struct pm_kboot_info *info, u32 ram, u32 root, u32 bat);
void pm_report_clock(u32 hz, u32 ticks, u32 us);
void pm_report_ready(unsigned long long start);
void pm_report_alive(unsigned long long now, unsigned long long start, u32 tick0,
                     const struct pm_kboot_info *info);

#endif
