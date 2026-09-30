/* ======================================================================== */
/*  CPU_CALIBRATE.C — CPU速度キャリブレーション                              */
/*                                                                          */
/*  PIT タイマー (100Hz = 10ms/tick) を基準として CPU の NOP ループ速度を     */
/*  起動時に計測する。Linux の bogomips と同じ発想。                         */
/*                                                                          */
/*  重要: キャリブレーションと遅延で**同一のループ関数**を使用し、           */
/*  反復あたりのコスト差による誤差を排除する。                               */
/*                                                                          */
/*  計測手順:                                                               */
/*    1. tick_count の変化を待ち、tick 境界に同期                            */
/*    2. 既知回数の nop_loop() を **経過が CALIBRATE_MIN_TICKS に届くまで**  */
/*       繰り返す (速い CPU では 1 周では 1 tick にも届かない)               */
/*    3. loops_per_tick = 合計ループ数 / 合計 tick で算出                    */
/*                                                                          */
/*  ⚠ **2 の繰り返しが本題。** 直す前は 1 周だけ回して tick で割っていた。   */
/*  コメントの想定は 8MHz (12.5 tick) / 33MHz (3 tick) だったが、実機の      */
/*  PC-9821Ra266 (266MHz) では 1 周 ≒ 1ms で 1 tick にも届かず、            */
/*  `elapsed = 0` が 1 に丸められて s_loops_per_tick が **実際の 1/7〜1/13** */
/*  になっていた。その結果 cpu_delay_us(5) が実際には 0.5µs 程度しか待たず、 */
/*  drivers/serial.c の送信ループが TxRDY を待てずに _halt() へ落ちて、      */
/*  速度に依らない 1 バイト約 2ms の固定費になっていた                       */
/*  (実機実測 2026-09-22: 9600 で 389B/s、38400 でも 437B/s)。              */
/*  **NP21/W でも同じ丸めは起きる。** 実測 rounds = 16 / ticks = 5 =        */
/*  1 周 ≒ 0.31 tick なので、旧コードの生の `elapsed` は **0**。             */
/*  `if (elapsed == 0) elapsed = 1;` の補正で 1 になり、200,000 loops/tick   */
/*  という値が入っていた (真値 640,000 の約 1/3.2)。実機 266MHz は 1 周が    */
/*  0.1 tick 未満でもっと深く 0 に落ち、1/7〜1/13 の桁になる。               */
/*  **どちらも生値は 0 で、補正が桁を決めていた** — 症状が出るかどうかの     */
/*  境目は「予算が 1 文字時間を割り込むか」だけ。                            */
/*  止め方と計算は kernel/cpu_calibrate_math.c (ホスト試験あり)。            */
/*                                                                          */
/*  注意: cpu_calibrate() は PIT 初期化後、_enable() 後に呼ぶこと。         */
/* ======================================================================== */

#include "cpu_calibrate.h"
#include "cpu_calibrate_math.h"
#include "io.h"

/* タイマーティックカウンタ (isr_stub.asm でインクリメント) */
extern volatile u32 tick_count;

/* キャリブレーション結果 (静的変数) */
static u32 s_loops_per_tick = 0;

/* 何周回ったか / 何 tick 測れたか。**kernel.map 越しに読めるよう意図的に
 * グローバル** (実機で「本当に 5 tick 測れたか」を確かめる唯一の手段)。 */
u32 cpu_calib_rounds = 0;
u32 cpu_calib_ticks  = 0;

/* CALIBRATE_LOOPS / CALIBRATE_MIN_TICKS / CALIBRATE_MAX_ROUNDS と
 * フォールバック値は kernel/cpu_calibrate_math.h にある ([C4])。 */

/* ======================================================================== */
/*  nop_loop — キャリブレーションと遅延の共通ループ                         */
/*                                                                          */
/*  noinline 指定により、キャリブレーションと遅延で完全に同一の              */
/*  マシンコードが実行されることを保証する。                                 */
/* ======================================================================== */
static void __attribute__((noinline)) nop_loop(u32 n)
{
    while (n > 0) {
        __asm__ volatile("nop");
        n--;
    }
}

/* ======================================================================== */
/*  cpu_calibrate — 共通ループの実測によるキャリブレーション                 */
/* ======================================================================== */
void cpu_calibrate(void)
{
    u32 start;
    u32 ticks;
    u32 rounds;
    u32 total;

    /* tick 境界に同期: 次の tick 開始まで待つ */
    start = tick_count;
    while (tick_count == start) {
        /* 何もしない */
    }

    /* **経過が CALIBRATE_MIN_TICKS に届くまで回す。**
     * 速い CPU では 1 周が 1 tick に満たないので、1 周で打ち切ると
     * 0 → 1 の丸めで実際の何分の 1 かの値が入る (ファイル冒頭の注記)。
     * 打ち切り (CALIBRATE_MAX_ROUNDS) は PIT が死んだときの保険で、
     * 通常の機械には掛からない (266MHz でも 50 周ほどで 5 tick)。 */
    start = tick_count;
    total = 0;
    ticks = 0;
    rounds = 0;
    do {
        nop_loop(CALIBRATE_LOOPS);
        total += CALIBRATE_LOOPS;
        rounds++;
        ticks = tick_count - start;
    } while (!cpu_calibrate_enough(ticks, rounds));

    cpu_calib_rounds = rounds;
    cpu_calib_ticks = ticks;
    s_loops_per_tick = cpu_calibrate_compute(total, ticks);
}

/* ======================================================================== */
/*  cpu_loops_per_tick — キャリブレーション結果を返す                        */
/* ======================================================================== */
u32 cpu_loops_per_tick(void)
{
    return s_loops_per_tick;
}

/* ======================================================================== */
/*  cpu_delay_us — CPU速度適応型マイクロ秒ディレイ                          */
/*                                                                          */
/*  1 tick = 10,000 µs                                                      */
/*  loops = loops_per_tick * us / 10000                                     */
/*                                                                          */
/*  オーバーフロー防止のため (lpt/100) * us / 100 で計算する。              */
/*  割り込み禁止区間でも使用可能 (PIT に依存しない)。                       */
/* ======================================================================== */
void cpu_delay_us(u32 us)
{
    u32 loops;

    if (s_loops_per_tick == 0) return;

    if (us > CPU_DELAY_US_MAX) {
        us = CPU_DELAY_US_MAX;
    }

    /* (lpt/100) * us / 100 でオーバーフロー回避しつつ精度確保 */
    loops = (s_loops_per_tick / 100UL) * us / 100UL;

    if (loops == 0) return;

    nop_loop(loops);
}
