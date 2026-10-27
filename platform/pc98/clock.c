/* PC-98 system clock selection; unrelated to the RTC registration table. */
#include "platform_timer.h"
#include "pc98.h"
#include "memmap.h"
#include "v86_bios.h"

void platform_clock_read(struct platform_clock_value *out)
{
    u8 v = v86_bios_real_lowmem()[BIOS_WORK_SYSCLK];
    out->is_8mhz = (u8)((v & BIOS_SYSCLK_8MHZ) ? 1 : 0);
    out->hz = out->is_8mhz ? (unsigned long)SYSCLK_1997 : (unsigned long)SYSCLK_2458;
}
