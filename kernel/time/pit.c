#include "../../arch/x86/kernel/idt.h"
#include "sysclk.h"
#include "../../include/platform_timer.h"

/* ======================================================================== */
/*  pit_init — PIT (8254) インターバルタイマ設定                            */
/*  カウンタ#0 をモード2(レートジェネレータ)で設定                          */
/*                                                                          */
/*  **リロード値は判定したシステムクロックから出す** (kernel/time/sysclk.c)。    */
/*  1.9968MHz → 19968 / 2.4576MHz → 24576。どちらもちょうど 10ms。          */
/*  決め打ちだったころ、2.4576MHz 系 (実機 PC-9821Ra266) の tick は         */
/*  8.125ms (123Hz) で、tick を数える待ち・番犬・CPU 校正が全部 23% 速く     */
/*  なっていた。NP21/W は 1.9968MHz 設定なので**ここは踏めない**            */
/*  (docs/POLICY_DEBUG.md §4-54)。                                          */
/*                                                                          */
/*  hz: 割り込み周波数。いまは PIT_HZ のみ。                                */
/*  戻り: 0 = 頼まれたとおり / 負 = 出せないので既定 PIT_HZ を積んだ         */
/* ======================================================================== */
static struct pit_setup s_pit = { 0, 0, 0, 0, 0, 0 };

const struct pit_setup *pit_get_setup(void)
{
    return &s_pit;
}

int pit_init(unsigned int hz)
{
    struct pit_setup s;
    int rc = pit_compute(sysclk_hz(), hz, &s);
    if (rc != 0 && pit_compute(sysclk_hz(), (unsigned int)PIT_HZ, &s) != 0)
        return rc; /* Both failed: preserve state and hardware. */
    s.mode = (u8)PIT_MODE_TIMER0;
    s_pit = s;
    platform_timer_program(s.reload);
    return rc; /* Preserve the original error after successful fallback. */
}
