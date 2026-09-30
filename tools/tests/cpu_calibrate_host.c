/* ======================================================================== */
/*  CPU_CALIBRATE_HOST.C — kernel/cpu_calibrate_math.c をホストで回す       */
/*                                                                          */
/*  実物の判定を 1 行も写さずに #include する。tick_count も I/O も触らない */
/*  ので模型は 1 つも要らない。                                             */
/*                                                                          */
/*  見るのは **エミュレータでは踏めない丸め** (記録: cpu_calibrate_tdd.md): */
/*    (a) 速い CPU (266MHz) で 1 周が 1 tick に満たないとき、何周でも回して */
/*        5 tick に届かせる — 直す前は 1 周で打ち切り、elapsed 0 → 1 の     */
/*        丸めで loops_per_tick が実際の 1/7〜1/13 になっていた             */
/*    (b) 遅い CPU (8MHz) は 1 周で 12.5 tick なので即確定 (退行していない) */
/*    (c) PIT が死んで tick が進まないときに戻ってこなくならないこと        */
/*                                                                          */
/*  NP21/W は十分に遅いので (a) を踏めない。実機 PC-9821Ra266 でだけ出た。  */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/cpu_calibrate_math.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* 校正ループを模した「回し切り」。`per_round_ticks_x100` は 1 周で進む
 * tick 数を 100 倍した整数 (266MHz なら 10 = 0.1 tick、8MHz なら 1250)。
 * 実物の cpu_calibrate() と**同じ止め方**で回し、確定値を返す。 */
static u32 spin(u32 per_round_ticks_x100, u32 *out_rounds, u32 *out_ticks)
{
    u32 rounds = 0;
    u32 acc_x100 = 0;
    u32 ticks = 0;
    u32 total = 0;

    do {
        total += (u32)CALIBRATE_LOOPS;
        rounds++;
        acc_x100 += per_round_ticks_x100;
        ticks = acc_x100 / 100;
    } while (!cpu_calibrate_enough(ticks, rounds));

    if (out_rounds) *out_rounds = rounds;
    if (out_ticks) *out_ticks = ticks;
    return cpu_calibrate_compute(total, ticks);
}

/* ------------------------------------------------------------------ */
/*  (a) 速い CPU — 1 周が 1 tick に満たない                             */
/* ------------------------------------------------------------------ */
static void fast_cpu(void)
{
    u32 rounds = 0, ticks = 0, lpt;

    /* 0 tick のうちは**必ず続行する**。ここを「1 周で打ち切り」にして
     * いたのが今回の原因で、実機 266MHz で loops_per_tick が実際の
     * 1/7〜1/13 になり、cpu_delay_us(5) が 0.5µs しか待たなかった。 */
    CHECK(cpu_calibrate_enough(0, 1) == 0);
    CHECK(cpu_calibrate_enough(0, 2) == 0);
    CHECK(cpu_calibrate_enough(0, 49) == 0);
    /* 途中の 1〜4 tick でも確定しない (1 tick は ±100% ぶれる)。 */
    CHECK(cpu_calibrate_enough(1, 10) == 0);
    CHECK(cpu_calibrate_enough(4, 40) == 0);
    /* 5 tick に届いたら確定。 */
    CHECK(cpu_calibrate_enough(CALIBRATE_MIN_TICKS, 50) == 1);
    CHECK(cpu_calibrate_enough(CALIBRATE_MIN_TICKS + 1, 51) == 1);

    /* 実機 PC-9821Ra266 相当: 1 周 (200,000 ループ) ≒ 1ms = 0.1 tick。
     * 50 周で 5 tick に届き、合計 1,000 万ループ → 200 万 loops/tick。 */
    lpt = spin(10, &rounds, &ticks);
    CHECK(rounds == 50);
    CHECK(ticks == (u32)CALIBRATE_MIN_TICKS);
    CHECK(lpt == 2000000UL);

    /* **直す前の値と比べる。** 1 周で打ち切ると 200,000 / 1 = 200,000 で、
     * 実際の 1/10 になる。10 倍ずれていたことをそのまま押さえる。 */
    CHECK(cpu_calibrate_compute(CALIBRATE_LOOPS, 1) == 200000UL);
    CHECK(lpt == 10UL * cpu_calibrate_compute(CALIBRATE_LOOPS, 1));

    /* もっと速い機械 (1 周 = 0.05 tick) でも 100 周で届く。 */
    lpt = spin(5, &rounds, &ticks);
    CHECK(rounds == 100);
    CHECK(ticks == (u32)CALIBRATE_MIN_TICKS);
    CHECK(lpt == 4000000UL);
}

/* ------------------------------------------------------------------ */
/*  (b) 遅い CPU — 1 周で足りる (退行していないこと)                    */
/* ------------------------------------------------------------------ */
static void slow_cpu(void)
{
    u32 rounds = 0, ticks = 0, lpt;

    /* 8MHz 相当: 1 周 ≒ 125ms = 12.5 tick。**1 周で確定**する。
     * ここが 2 周以上になると遅い機械の起動が目に見えて延びる。 */
    lpt = spin(1250, &rounds, &ticks);
    CHECK(rounds == 1);
    CHECK(ticks == 12);
    CHECK(lpt == CALIBRATE_LOOPS / 12);

    /* 33MHz 相当: 1 周 ≒ 30ms = 3 tick。5 に届かないので 2 周目へ。 */
    lpt = spin(300, &rounds, &ticks);
    CHECK(rounds == 2);
    CHECK(ticks == 6);
    CHECK(lpt == (2UL * CALIBRATE_LOOPS) / 6);

    /* ちょうど 5 tick で終わる機械も 1 周。 */
    CHECK(cpu_calibrate_enough(12, 1) == 1);
    CHECK(cpu_calibrate_enough(5, 1) == 1);
}

/* ------------------------------------------------------------------ */
/*  (c) 打ち切りと測れなかったとき                                      */
/* ------------------------------------------------------------------ */
static void give_up(void)
{
    u32 rounds = 0, ticks = 0, lpt;

    /* PIT が死んで tick が永久に進まない。**戻ってこなければ起動が止まる。**
     * 上限の周回数で打ち切ること。 */
    CHECK(cpu_calibrate_enough(0, CALIBRATE_MAX_ROUNDS) == 1);
    CHECK(cpu_calibrate_enough(0, CALIBRATE_MAX_ROUNDS + 1) == 1);
    CHECK(cpu_calibrate_enough(0, CALIBRATE_MAX_ROUNDS - 1) == 0);

    lpt = spin(0, &rounds, &ticks);
    CHECK(rounds == (u32)CALIBRATE_MAX_ROUNDS);
    CHECK(ticks == 0);
    /* **合計を 1 で割らない。** 200 周 × 20 万 = 4000 万を 1 tick と答えると、
     * cpu_delay_us がその比で回って永久に返らなくなる。 */
    CHECK(lpt == (u32)CALIBRATE_FALLBACK_LPT);
    CHECK(lpt != 40000000UL);

    /* 測れなかった値 (0 tick) はフォールバックへ。 */
    CHECK(cpu_calibrate_compute(0, 0) == (u32)CALIBRATE_FALLBACK_LPT);
    CHECK(cpu_calibrate_compute(40000000UL, 0) == (u32)CALIBRATE_FALLBACK_LPT);

    /* 極端に小さい結果も「測れていない」。 */
    CHECK(cpu_calibrate_compute(999, 1) == (u32)CALIBRATE_FALLBACK_LPT);
    CHECK(cpu_calibrate_compute(CALIBRATE_MIN_LPT, 1) == (u32)CALIBRATE_MIN_LPT);

    /* 下限ちょうどは通す (フォールバックへ倒さない)。 */
    CHECK(cpu_calibrate_compute(5000, 5) == 1000UL);
}

/* ------------------------------------------------------------------ */
/*  (d) 結果が cpu_delay_us にどう効くか                               */
/* ------------------------------------------------------------------ */
static void delay_scale(void)
{
    /* cpu_delay_us の式 (kernel/cpu_calibrate.c):
     *   loops = (lpt / 100) * us / 100        (1 tick = 10,000µs)
     * 実機 266MHz で正しい lpt = 200 万のとき、5µs は 1000 ループ。 */
    u32 good = 2000000UL;
    u32 bad  = 200000UL;          /* 直す前 (1 周で打ち切った値) */

    CHECK((good / 100UL) * 5UL / 100UL == 1000UL);
    /* 直す前は同じ 5µs の要求で 100 ループ = **実際には 0.5µs**。 */
    CHECK((bad / 100UL) * 5UL / 100UL == 100UL);
    CHECK((good / 100UL) * 5UL / 100UL
          == 10UL * ((bad / 100UL) * 5UL / 100UL));

    /* **つまり cpu_delay_us の利用者は実待ちが 10 倍に伸びる。**
     * drivers/serial.c (TxRDY のポーリング間隔)、drivers/ne2000.c
     * (RDC / RESET / OVW)、gfx/gfx_vram.c (RASTER_LINE_US) の 3 つ。
     * いままで**短すぎた**ので、伸びるのが正しい向き。 */
    CHECK(good > bad);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    failed = 0;
    if (!strcmp(argv[1], "fast_cpu")) fast_cpu();
    else if (!strcmp(argv[1], "slow_cpu")) slow_cpu();
    else if (!strcmp(argv[1], "give_up")) give_up();
    else if (!strcmp(argv[1], "delay_scale")) delay_scale();
    else return 2;
    if (failed) return 1;
    printf("PASS %s\n", argv[1]);
    return 0;
}
