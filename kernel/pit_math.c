/* ======================================================================== */
/*  PIT_MATH.C — PIT カウンタ#0 のリロード値と実周期                        */
/*                                                                          */
/*  副作用のある行を 1 つも置かない (I/O も tick_count も見ない)。          */
/*  切り出してあるのは、**エミュレータでは踏めないクロック依存**をホストで  */
/*  試験するため。NP21/W は 1.9968MHz 固定なので、2.4576MHz 系の 24576 は   */
/*  エミュレータでは一度も通らない。                                        */
/*    試験: tools/tests/test_pit_clock.py                                   */
/*    記録: tools/tests/pit_clock_tdd.md                                    */
/* ======================================================================== */

#include "pit_math.h"
#include "pc98.h"

/* 計算は `unsigned int` (i386 でもホストでも 32bit) で行う。u32 =
 * unsigned long はホストでは 64bit になり、溢れが再現しない。 */
STATIC_ASSERT(sizeof(unsigned int) == 4, pit_uint_is_32bit);
/* 約分が等価であること。崩すと period_us が µs でなくなる。 */
STATIC_ASSERT(PIT_PERIOD_SCALE * PIT_CLOCK_DIV == PIT_US_PER_SEC,
              pit_period_scale_is_one_second);
/* 中間積が 32bit に収まること。**i386 のビルドで効く** — 倍率を 10^6 に
 * 戻すと、ここが「負サイズの配列」でコンパイルエラーになる。 */
STATIC_ASSERT((PIT_RELOAD_MAX * PIT_PERIOD_SCALE) / PIT_PERIOD_SCALE
              == PIT_RELOAD_MAX, pit_period_product_fits_u32);
/* クロックが約分できること (割り切れないと µs がずれる)。 */
STATIC_ASSERT(SYSCLK_1997 % PIT_CLOCK_DIV == 0, pit_clk1997_divisible);
STATIC_ASSERT(SYSCLK_2458 % PIT_CLOCK_DIV == 0, pit_clk2458_divisible);

int pit_compute(unsigned long clk_hz, unsigned int hz, struct pit_setup *out)
{
    unsigned long raw;
    unsigned int reload;
    unsigned int period;

    if (out == 0) {
        return PIT_ERR_ARG;
    }

    /* 失敗したら「積んでいない」と分かる形で返す。呼び出し側が戻り値を
     * 見落としても、valid = 0 の古い値を本物と読まない。 */
    out->clk_hz = 0;
    out->hz = 0;
    out->reload = 0;
    out->period_us = 0;
    out->mode = 0;
    out->valid = 0;

    /* この票で扱うのは 100Hz だけ。1kHz 版 (§5-5) はここを広げる。 */
    if (hz != (unsigned int)PIT_HZ) {
        return PIT_ERR_HZ;
    }

    /* 知らないクロックで割らない。**既定へ黙って倒すと直す前と同じ** で、
     * 2.4576MHz 機が 1.9968MHz のリロード値のまま走る。 */
    if (clk_hz != (unsigned long)SYSCLK_1997 &&
        clk_hz != (unsigned long)SYSCLK_2458) {
        return PIT_ERR_CLOCK;
    }

    raw = clk_hz / (unsigned long)hz;
    if (raw == 0 || raw > (unsigned long)PIT_RELOAD_MAX) {
        return PIT_ERR_RELOAD;
    }
    reload = (unsigned int)raw;

    /* 実周期 [µs] = reload × 10^6 ÷ clk_hz。**そのまま掛けると u32 が溢れる**
     * (24576 × 10^6 = 2.4576×10^10)。クロックは必ず PIT_CLOCK_DIV で
     * 割り切れるので、先に両辺を約してから割る (pit_math.h の注記)。
     *   1.9968MHz: 19968 × 10000 ÷ 19968 = 10000µs
     *   2.4576MHz: 24576 × 10000 ÷ 24576 = 10000µs */
    period = (reload * PIT_PERIOD_SCALE) / (unsigned int)(clk_hz / PIT_CLOCK_DIV);

    out->clk_hz = clk_hz;
    out->hz = hz;
    out->reload = reload;
    out->period_us = period;
    out->valid = 1;
    return 0;
}
