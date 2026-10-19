/* ======================================================================== */
/*  SYSCLK.C — BIOS ワークエリア 0000:0501h bit7 のクロック判定             */
/*                                                                          */
/*  読むのは起動時の 1 回だけ (kernel.c、paging_init より前)。以後の利用者   */
/*  (kernel/idt.c の pit_init / drivers/serial.c) は保存値だけを見る。       */
/*  経緯と検証は docs/POLICY_DEBUG.md §4-54。                               */
/* ======================================================================== */

#include "sysclk.h"
#include "memmap.h"
#include "pc98.h"
#include "v86_bios.h"

/* 未判定のときの既定は従来どおり 1.9968MHz (NP21/W と 8MHz 系の値)。
 * ここを 0 にすると、万一 detect を呼び忘れたときに分周が 0 になる。 */
static unsigned long s_hz = SYSCLK_1997;
static u8 s_is_8mhz = 0;
static u8 s_detected = 0;

void sysclk_detect(void)
{
    u8 v;

    if (s_detected) {
        return;
    }
    v = v86_bios_real_lowmem()[BIOS_WORK_SYSCLK];
    s_is_8mhz = (u8)((v & BIOS_SYSCLK_8MHZ) ? 1 : 0);
    s_hz = s_is_8mhz ? (unsigned long)SYSCLK_1997 : (unsigned long)SYSCLK_2458;
    s_detected = 1;
}

unsigned long sysclk_hz(void)
{
    return s_hz;
}

int sysclk_is_8mhz(void)
{
    return (int)s_is_8mhz;
}

int sysclk_detected(void)
{
    return (int)s_detected;
}
