/* ======================================================================== */
/*  PIT_MATH.H — PIT (8254) カウンタ#0 のリロード値と周期だけ               */
/*                                                                          */
/*  I/O も tick_count も触らない決め事だけを kernel/idt.c から切り出して     */
/*  ある。**ここが間違っていたことはエミュレータでは見えなかった** —        */
/*  NP21/W は 1.9968MHz 設定なので、クロック決め打ちでも 100Hz ちょうどに    */
/*  なる。2.4576MHz 系 (実機 PC-9821Ra266) では 123Hz (8.125ms) だった。     */
/*    試験: tools/tests/test_pit_clock.py                                   */
/*    記録: tools/tests/pit_clock_tdd.md                                    */
/*    票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-0                           */
/* ======================================================================== */

#ifndef PIT_MATH_H
#define PIT_MATH_H

#include "types.h"
#include "memmap.h"   /* PIT_HZ (タイマ割り込み周波数 100Hz) */

/* 8254 のカウンタは 16bit。0 は「65536」を意味するが、この票の範囲では
 * どちらのクロックでも 19968 / 24576 なので使わない。 */
#define PIT_RELOAD_MAX   65535UL

/* 1 秒 = 10^6 µs。**`unsigned int` で持つ** — `UL` にすると 64bit ホストでは
 * `reload × PIT_US_PER_SEC` が溢れず、ホスト試験が「素直に 10^6 を掛ける」
 * 退行を見逃す (変異 4)。i386 では unsigned long も 32bit なので同じ。 */
#define PIT_US_PER_SEC   1000000U

/* period_us = reload × 10^6 ÷ clk_hz を **32bit で溢れずに** 出すための分解。
 * `reload × 10^6` は 24576 でも 2.4576×10^10 で u32 (i386 の unsigned long)
 * を越える。クロックは必ず PIT_CLOCK_DIV で割り切れるので、先に両辺を
 * 約して `reload × PIT_PERIOD_SCALE ÷ (clk_hz ÷ PIT_CLOCK_DIV)` にする。
 * 中間積の最大は PIT_RELOAD_MAX × PIT_PERIOD_SCALE = 6.5535×10^8。
 * **PIT_PERIOD_SCALE × PIT_CLOCK_DIV == PIT_US_PER_SEC** が前提
 * (kernel/pit_math.c の STATIC_ASSERT で固定)。
 * **倍率は `unsigned int` (どの環境でも 32bit)** で持つ。`unsigned long` に
 * すると 64bit ホストでは溢れが再現せず、ホスト試験が素通しになる。 */
#define PIT_PERIOD_SCALE 10000U
#define PIT_CLOCK_DIV    100UL

/* i386 の u32 の上限。ホスト試験は 64bit の unsigned long で回るので、
 * 中間積が 32bit に収まるかは**この値と比べて**確かめる。 */
#define PIT_U32_MAX      4294967295UL

/* pit_compute の戻り値 (負はすべて「頼まれた設定は出せない」)。 */
#define PIT_ERR_ARG      (-1)   /* out == NULL */
#define PIT_ERR_HZ       (-2)   /* この票が扱うのは PIT_HZ だけ */
#define PIT_ERR_CLOCK    (-3)   /* 既知の 2 つ以外のクロック */
#define PIT_ERR_RELOAD   (-4)   /* 16bit カウンタに収まらない */

/* 実際に積んだ設定。kselftest と 1-5 (µs 時計) がここを読む。
 * `mode` は I/O を出す側 (kernel/idt.c) が入れる — ここは算数だけ。 */
struct pit_setup {
    unsigned long clk_hz;      /* 元になったシステムクロック [Hz] */
    unsigned int  hz;          /* 頼んだ割り込み周波数 [Hz] */
    unsigned int  reload;      /* カウンタ#0 に積む値 */
    unsigned int  period_us;   /* 1 tick の実周期 [µs] */
    u8 mode;                   /* モードバイト (PIT_MODE_TIMER0) */
    u8 valid;                  /* 1 = 積んである */
};

/* clk_hz / hz からリロード値と実周期を出す。
 *   戻り 0      : out を埋めた (valid = 1)
 *   戻り 負     : 出せない。out は valid = 0 で潰す (中途半端な値を残さない)
 * **period_us は 32bit で溢れない式で出す** — reload × 1000000 は
 * 24576 でも 2.4×10^10 で u32 を越える。 */
int pit_compute(unsigned long clk_hz, unsigned int hz, struct pit_setup *out);

#endif /* PIT_MATH_H */
