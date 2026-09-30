/* ======================================================================== */
/*  CPU_CALIBRATE.H — CPU速度キャリブレーション                              */
/*                                                                          */
/*  PIT (100Hz) を基準に CPU の NOP ループ速度を計測し、                     */
/*  CPU速度に適応したマイクロ秒ディレイ関数を提供する。                     */
/* ======================================================================== */

#ifndef CPU_CALIBRATE_H
#define CPU_CALIBRATE_H

#include "types.h"

/* カーネル起動時に1回だけ呼ぶ。PIT初期化後、割り込み許可後に実行すること */
void cpu_calibrate(void);

/* キャリブレーション結果: 1 tick (10ms) あたりの NOP ループ回数 */
u32 cpu_loops_per_tick(void);

/* 校正が実際に何周回って何 tick 測れたか (診断用)。**kernel.map 越しに
 * 読めるよう意図的にグローバル** — 実機で「丸めが起きていない」ことを
 * 確かめる唯一の手段 (266MHz なら rounds ≒ 50 / ticks = 5、
 * NP21/W なら rounds = 1 / ticks >= 5)。 */
extern u32 cpu_calib_rounds;
extern u32 cpu_calib_ticks;

/* cpu_delay_us が 1 回で待てる上限 (µs)。これより長い指定は**黙ってこの値に
 * 丸める** — 長く待ちたい呼び手はこれ以下の塊に分けて呼ぶ (drivers/atapi.c の
 * atapi_delay_us はこのヘッダを直接 include して CPU_DELAY_US_MAX ずつに分ける) */
#define CPU_DELAY_US_MAX  100000UL

/* CPU速度適応型マイクロ秒ディレイ
 * キャリブレーション結果を基に、指定マイクロ秒だけ NOP ループで待つ。
 * 割り込み禁止区間でも使用可能 (PIT に依存しない)。
 * 精度: ±10% 程度 (キャリブレーション誤差 + パイプライン変動)。
 * us は CPU_DELAY_US_MAX で丸める */
void cpu_delay_us(u32 us);

#endif /* CPU_CALIBRATE_H */
