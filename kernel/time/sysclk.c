/* ======================================================================== */
/*  SYSCLK.C — BIOS ワークエリア 0000:0501h bit7 のクロック判定             */
/*                                                                          */
/*  読むのは起動時の 1 回だけ (kernel.c、paging_init より前)。以後の利用者   */
/*  (kernel/time/pit.c の pit_init / drivers/char/serial.c) は保存値だけを見る。       */
/*  経緯と検証は docs/POLICY_DEBUG.md#4-54-pit-の実時間。                               */
/* ======================================================================== */

#include "sysclk.h"
#include "pc98.h"
#include "../../include/platform_timer.h"

/* 未判定のときの既定は従来どおり 1.9968MHz (NP21/W と 8MHz 系の値)。
 * ここを 0 にすると、万一 detect を呼び忘れたときに分周が 0 になる。 */
static unsigned long s_hz = SYSCLK_1997;
static u8 s_is_8mhz = 0;
static u8 s_detected = 0;

void sysclk_detect(void)
{
    struct platform_clock_value v;

    if (s_detected) {
        return;
    }
    platform_clock_read(&v);
    s_is_8mhz = v.is_8mhz;
    s_hz = v.hz;
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
