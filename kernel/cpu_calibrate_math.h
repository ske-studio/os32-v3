/* ======================================================================== */
/*  CPU_CALIBRATE_MATH.H — 校正の「いつ止めるか」と「いくつか」だけ         */
/*                                                                          */
/*  I/O も tick_count も触らない決め事だけを kernel/cpu_calibrate.c から    */
/*  切り出してある。**ここが間違っていたことは実機でしか見えなかった** —    */
/*  エミュレータでも 1 周では 5 tick に届かない (NP21/W 実測 rounds = 16 /  */
/*  1 周 ≒ 0.31 tick)。**旧コードの生の elapsed はそこでも 0** で、補正      */
/*  (0 → 1) が 200,000 を入れていた (真値 640,000 の約 1/3.2)。実機 266MHz   */
/*  は 1 周が 0.1 tick 未満でもっと深く 0 に落ち、1/7〜1/13 の桁になる。      */
/*    試験: tools/tests/test_cpu_calibrate.py                               */
/*    記録: tools/tests/cpu_calibrate_tdd.md                                */
/*    票  : docs/archive/realhw_v21/TASK_SERIAL_VFAST.md (往復 3)                 */
/*                                                                          */
/*  直す前は `nop_loop(CALIBRATE_LOOPS)` を **1 回だけ** 回して tick で     */
/*  割っていた。コメントの想定は 8MHz (12.5 tick) / 33MHz (3 tick) だが、   */
/*  実機の PC-9821Ra266 (266MHz) では 1 tick にも届かず `elapsed = 0` が    */
/*  1 に丸められ、`s_loops_per_tick` が**実際の 1/7〜1/13**になっていた。   */
/*  その結果 `cpu_delay_us(5)` が実際には 0.5µs 程度しか待たず、            */
/*  `serial_putchar` が TxRDY を待てずに `_halt()` へ落ちて、               */
/*  速度に依らない 1 バイト約 2ms の固定費になっていた                      */
/*  (実機実測: 9600 で 389B/s、38400 でも 437B/s)。                         */
/* ======================================================================== */

#ifndef CPU_CALIBRATE_MATH_H
#define CPU_CALIBRATE_MATH_H

#include "types.h"

/* 1 周あたりの nop ループ回数。
 *   8MHz  想定: 1 周 ≒ 125ms (12.5 tick) → 1 周で確定
 *   266MHz 実測: 1 周 ≒ 1ms (0.1 tick)   → 50 周ほど回して 5 tick に届く */
#define CALIBRATE_LOOPS       200000UL

/* **確定に必要な最小 tick 数。** 1 tick しか測れないと ±100% の誤差になる
 * (tick は「境界を何回跨いだか」なので、最初の 1 回は 0〜10ms のどこでも
 * 立ちうる)。5 tick なら誤差は ±20% に収まり、cpu_delay_us の謳う精度
 * (±10% 程度) と同じ桁になる。 */
#define CALIBRATE_MIN_TICKS   5UL

/* **周回数の上限 (安全装置)。** PIT が止まっていると tick が永久に進まない
 * ので、これが無いと `cpu_calibrate()` が起動を止める。266MHz でも 5 tick に
 * 届くのは 50 周ほどなので、200 周は通常経路には掛からない。
 * 8MHz なら 1 周目で確定するので、遅い機械が 200 周 × 125ms 待つことはない。 */
#define CALIBRATE_MAX_ROUNDS  200UL

/* 測れなかったときの値と、その下限。1000 未満は「測れていない」と見なす。 */
#define CALIBRATE_MIN_LPT      1000UL
#define CALIBRATE_FALLBACK_LPT 10000UL

/* もう測り終えてよいか。
 *   ticks  : 同期した tick 境界からの経過 tick 数
 *   rounds : これまでに回した nop_loop(CALIBRATE_LOOPS) の回数
 * 戻り 1 = 止めてよい / 0 = もう 1 周回す。
 * **rounds を引数に取るのは打ち切りのため** — PIT が死んでいると ticks は
 * 永久に 0 のままで、ticks だけを見る判定は戻ってこない。 */
int cpu_calibrate_enough(u32 ticks, u32 rounds);

/* 合計ループ数と経過 tick から 1 tick あたりのループ回数を出す。
 * ticks == 0 (打ち切りまで tick が進まなかった = PIT が死んでいる) のときは
 * **合計で割らずに** フォールバック値を返す。割ると「1 tick で 4000 万周」に
 * なり、cpu_delay_us が永久に返らなくなる。 */
u32 cpu_calibrate_compute(u32 total_loops, u32 ticks);

#endif /* CPU_CALIBRATE_MATH_H */
