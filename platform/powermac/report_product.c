#include "kboot_report.h"

const int pm_report_successes = 0;

void pm_report_handoff(const struct pm_kboot_info *info, u32 ram, u32 root, u32 bat)
{
    (void)ram;
    (void)info;
    (void)root;
    (void)bat;
}

void pm_report_clock(u32 hz, u32 ticks, u32 us)
{
    (void)hz;
    (void)ticks;
    (void)us;
}

void pm_report_ready(unsigned long long start)
{
    (void)start;
}

void pm_report_alive(unsigned long long now, unsigned long long start, u32 tick0,
                     const struct pm_kboot_info *info)
{
    (void)now;
    (void)start;
    (void)tick0;
    (void)info;
}
