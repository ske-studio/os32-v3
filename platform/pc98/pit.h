#ifndef PC98_PIT_H
#define PC98_PIT_H
#include "pc98.h"
#include "pit_math.h"
#define PIT_CNTR0    PIT_CNT0  /* カウンタ#0 (インターバルタイマ) */
#define PIT_CNTR1    PIT_CNT1_BEEP /* カウンタ#1 (スピーカー) — 特殊アドレス */
#define PIT_CNTR2    PIT_CNT2  /* カウンタ#2 (RS-232C) */
/* PIT_MODE is defined in pc98.h. */

/* システムクロックは機種で 2 通り (1.9968MHz / 2.4576MHz)。**決め打ちしない** */
/* — 判定は kernel/time/sysclk.c、分周の算数は platform/pc98/pit_math.c。               */
/* 直す前はここに PIT_CLOCK = 1996800 があり、2.4576MHz 系では 100Hz のつもり */
/* の tick が 123Hz (8.125ms) になっていた (docs/POLICY_DEBUG.md#4-54-pit-の実時間)。    */

/* PITモードバイト: カウンタ#0, LSB/MSB, モード2(レートジェネレータ), バイナリ */
#define PIT_MODE_TIMER0  0x34  /* 00 11 010 0 */

/* カウンタラッチコマンド (SC=00 カウンタ#0, RW=00 = ラッチ)。
 * 走っているカウンタを止めずに現在値を凍らせ、下位 → 上位の順に読む。
 * µs 時計 (kernel/time/ktime.c) が使う。 */
#define PIT_LATCH_TIMER0 0x00  /* 00 00 000 0 */

/* カウンタ#0 を hz で回す。戻り 0 = 頼まれたとおり / 負 = 出せないので
 * **既定 PIT_HZ を積んだ** (タイマ無しで起動を続けない)。いまは PIT_HZ
 * 以外を受けない (票 TASK_HAL_WIRING §1-0)。 */
int pit_init(unsigned int hz);

/* platform/pc98/pit.c が所有する、直前の pit_init が積んだ設定。valid = 0 なら一度も積んでいない。 */
const struct pit_setup *pit_get_setup(void);


#endif
