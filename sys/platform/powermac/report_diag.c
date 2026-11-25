#include "kboot_report.h"
#include "kprintf.h"
#include "kboot_info.h"
#include "kernel_tick.h"
#include "ofw.h"

const int pm_report_successes = 1;
volatile unsigned long long kboot_ready_tb;

void pm_report_handoff(const struct pm_kboot_info *info, u32 ram, u32 root, u32 bat)
{
    kprintf(0x07, "KBOOT OF-CAPTURE ram=%u admitted=%u tb_hz=%u\r\n",
            ram, info->admitted_end, info->tb_hz);
    kprintf(0x07, "KBOOT OF-OFF calls=%u\r\n", info->of_calls_at_detach);
    kprintf(0x07, "KBOOT MMU-D root=0x%x hpt=0x%x bytes=%u ram_bat=%u\r\n",
            root, info->hpt_pa, info->hpt_bytes, bat);
}

void pm_report_clock(u32 hz, u32 ticks, u32 us)
{
    kprintf(0x07, "KBOOT CLOCK hz=%u tick=%u us=%u\r\n", hz, ticks, us);
}

void pm_report_ready(unsigned long long start)
{
    kboot_ready_tb = start;
    kprintf(0x07, "KBOOT READY stage=pre-user\r\n");
}

void pm_report_alive(unsigned long long now, unsigned long long start, u32 tick0,
                     const struct pm_kboot_info *info)
{
    u32 hz = info->tb_hz;
    u32 tick_delta = tick_count - tick0;
    unsigned long long delta = now - start;
    u32 tb_us = (u32)(delta / hz * 1000000 + delta % hz * 1000000 / hz);
    kprintf(0x07, "KBOOT ALIVE tick_delta=%u tb_us=%u of_calls_delta=%u\r\n",
            tick_delta, tb_us, ofw_call_count() - info->of_calls_at_detach);
}
