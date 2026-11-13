/* ======================================================================== */
/*  PIT_CLOCK_HOST.C — platform/pc98/pit_math.c をそのままホストで回す             */
/*                                                                          */
/*  実物の算数を 1 行も写さずに #include する。pit_math.c は I/O も          */
/*  tick_count も触らないので、模型は 1 つも要らない。                      */
/*                                                                          */
/*  見るのは **NP21/W では一度も通らない** 分岐 (記録: pit_clock_tdd.md):   */
/*    (a) 2.4576MHz 系 (実機 PC-9821Ra266) のリロード値 24576               */
/*    (b) どちらのクロックでも周期がちょうど 10000µs                        */
/*    (c) 100 / 1000Hz 以外と未知のクロックを**既定へ倒さずに断る**                */
/*    (d) 中間積が 32bit に収まること (ホストの long は 64bit なので、       */
/*        溢れそのものは踏めない — 上限と比べて確かめる)                    */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../platform/pc98/pit_math.c"
#include "../../platform/pc98/pit.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

static struct pit_setup self_setup;
static unsigned long self_clock;
static unsigned int self_rate;
const struct pit_setup *pit_get_setup(void) { return &self_setup; }
static int sysclk_detected(void) { return 1; }
static unsigned long sysclk_hz(void) { return self_clock; }
static void check(int ok, const char *why) { if (!ok) { fprintf(stderr, "FAIL %s\n", why); failed++; } }
#undef PIT_IRQ_HZ
#define PIT_IRQ_HZ self_rate
#include "../../kdebug/kselftest/pit_setup.inc"

static void selftest_setup(void)
{
    const unsigned long clocks[] = { SYSCLK_1997, SYSCLK_2458 };
    const unsigned int rates[] = { 100, 1000 };
    for (unsigned int i=0; i<2; i++) for (unsigned int j=0; j<2; j++) {
        self_clock=clocks[i]; self_rate=rates[j];
        CHECK(!pit_compute(self_clock, self_rate, &self_setup));
        self_setup.mode=PIT_MODE_TIMER0;
        test_pit_setup();
        CHECK(failed == 0);
        self_setup.period_us++;
        int before=failed;
        test_pit_setup();
        CHECK(failed > before);
        failed=before;
        self_setup.period_us--;
        self_setup.reload--;
        before=failed;
        test_pit_setup();
        CHECK(failed > before);
        failed=before;
    }
}


/* 期待するリロード値。**式ではなく数で書く** — 実装と同じ式で期待値を作ると
 * 両方が同じだけずれたときに気づけない。 */
#define RELOAD_1997   19968UL
#define RELOAD_2458   24576UL
#define PERIOD_100HZ  10000UL

/* ------------------------------------------------------------------ */
/*  (a)(b) 両方のクロック                                              */
/* ------------------------------------------------------------------ */
static void both_clocks(void)
{
    struct pit_setup s;

    /* 1.9968MHz (8MHz 系 / NP21/W の既定)。ここは直す前から合っていた。 */
    CHECK(pit_compute(SYSCLK_1997, (unsigned int)PIT_HZ, &s) == 0);
    CHECK(s.valid == 1);
    CHECK(s.clk_hz == SYSCLK_1997);
    CHECK(s.hz == (unsigned int)PIT_HZ);
    CHECK(s.reload == RELOAD_1997);
    CHECK(s.period_us == PERIOD_100HZ);

    /* 2.4576MHz (5/10MHz 系 / 実機 PC-9821Ra266)。**直す前はここが 19968** で、
     * 100Hz のつもりの tick が 123Hz になっていた。 */
    CHECK(pit_compute(SYSCLK_2458, (unsigned int)PIT_HZ, &s) == 0);
    CHECK(s.valid == 1);
    CHECK(s.clk_hz == SYSCLK_2458);
    CHECK(s.reload == RELOAD_2458);
    CHECK(s.period_us == PERIOD_100HZ);

    /* 2 つのリロード値は別物。同じなら片方のクロックを見ていない。 */
    CHECK(RELOAD_1997 != RELOAD_2458);

    /* どちらも 16bit カウンタに収まる (0 は 65536 の意味なので 0 も不可)。 */
    CHECK(RELOAD_1997 > 0 && RELOAD_1997 <= PIT_RELOAD_MAX);
    CHECK(RELOAD_2458 > 0 && RELOAD_2458 <= PIT_RELOAD_MAX);
}

/* ------------------------------------------------------------------ */
/*  (c) 100 / 1000Hz 以外を断る                                               */
/* ------------------------------------------------------------------ */
static void reject_hz(void)
{
    struct pit_setup s;

    CHECK(pit_compute(SYSCLK_1997, 1000, &s) == 0);
    CHECK(s.valid == 1 && s.hz == 1000 && s.reload == 1997);
    CHECK(pit_compute(SYSCLK_2458, 1000, &s) == 0);
    CHECK(s.valid == 1 && s.hz == 1000 && s.reload == 2458);

    /* 0Hz は 0 除算になるので必ず断る。 */
    CHECK(pit_compute(SYSCLK_1997, 0, &s) < 0);
    CHECK(s.valid == 0);

    /* 18.2Hz (PC/AT の既定) や 60Hz も、この票では受けない。 */
    CHECK(pit_compute(SYSCLK_1997, 18, &s) < 0);
    CHECK(pit_compute(SYSCLK_2458, 60, &s) < 0);
    CHECK(pit_compute(SYSCLK_1997, 99, &s) < 0);
    CHECK(pit_compute(SYSCLK_1997, 101, &s) < 0);

    /* 断り方は「種別の分かる負」で、成功の 0 とは別。 */
    CHECK(pit_compute(SYSCLK_1997, 101, &s) == PIT_ERR_HZ);
}

/* ------------------------------------------------------------------ */
/*  (c) 知らないクロックを断る (既定へ黙って倒さない)                   */
/* ------------------------------------------------------------------ */
static void reject_clock(void)
{
    struct pit_setup s;

    /* PC/AT の 1.193182MHz。PC-98 には無い。 */
    CHECK(pit_compute(1193182UL, (unsigned int)PIT_HZ, &s) == PIT_ERR_CLOCK);
    CHECK(s.valid == 0);
    /* 0 (判定に失敗した値がそのまま来た場合)。0 除算にしない。 */
    CHECK(pit_compute(0UL, (unsigned int)PIT_HZ, &s) == PIT_ERR_CLOCK);
    CHECK(s.valid == 0);
    /* 1 ずれた値。「だいたい合っている」で通さない。 */
    CHECK(pit_compute(SYSCLK_1997 + 1UL, (unsigned int)PIT_HZ, &s) < 0);
    CHECK(pit_compute(SYSCLK_2458 - 1UL, (unsigned int)PIT_HZ, &s) < 0);
    /* 桁違いの値も同じ口で断る (16bit に収まる前に白表で落ちる)。 */
    CHECK(pit_compute(SYSCLK_2458 * 1000UL, (unsigned int)PIT_HZ, &s)
          == PIT_ERR_CLOCK);
    CHECK(s.valid == 0);

    /* **既定へ倒していないこと。** 倒すと 2.4576MHz 機が直す前と同じ
     * 19968 のまま走り、誰も気づかない。 */
    CHECK(s.reload != RELOAD_1997);
    CHECK(s.clk_hz == 0);

    /* out == NULL でも落ちない。 */
    CHECK(pit_compute(SYSCLK_1997, (unsigned int)PIT_HZ, (struct pit_setup *)0)
          == PIT_ERR_ARG);
}

/* ------------------------------------------------------------------ */
/*  (d) 32bit で溢れない式であること                                    */
/*                                                                      */
/*  ホストの unsigned long は 64bit なので、**溢れそのものはここでは      */
/*  踏めない**。踏めるのは「中間積が 32bit の上限を越えないか」だけ。     */
/*  i386 のビルドでは platform/pc98/pit_math.c の STATIC_ASSERT が同じことを     */
/*  コンパイル時に見る (--target で通る)。                               */
/* ------------------------------------------------------------------ */
static void no_overflow(void)
{
    unsigned long worst;

    /* 約分が等価 (10^4 × 10^2 = 10^6)。崩れると µs でなくなる。 */
    CHECK(PIT_PERIOD_SCALE * PIT_CLOCK_DIV == USEC_PER_SEC);

    /* 最悪の中間積。**素直に 10^6 を掛けるとここが溢れる**
     * (65535 × 10^6 = 6.5535×10^10)。 */
    worst = PIT_RELOAD_MAX * PIT_PERIOD_SCALE;
    CHECK(worst <= PIT_U32_MAX);
    CHECK(PIT_RELOAD_MAX * USEC_PER_SEC > PIT_U32_MAX);

    /* 実際に使う 2 つの値も当然収まる。 */
    CHECK(RELOAD_1997 * PIT_PERIOD_SCALE <= PIT_U32_MAX);
    CHECK(RELOAD_2458 * PIT_PERIOD_SCALE <= PIT_U32_MAX);

    /* クロックは約分できる (割り切れないと µs がずれる)。 */
    CHECK(SYSCLK_1997 % PIT_CLOCK_DIV == 0);
    CHECK(SYSCLK_2458 % PIT_CLOCK_DIV == 0);
}

/* ------------------------------------------------------------------ */
/*  実機 PC-9821Ra266 の筋書きをそのまま並べる                          */
/* ------------------------------------------------------------------ */
static void real_hw_story(void)
{
    struct pit_setup s;
    unsigned long before;

    /* 1. 0000:0501h bit7 = 0 → 2.4576MHz 系。
     *    (判定そのものは kernel/time/sysclk.c。ここは決まったあとの算数。) */
    CHECK(pit_compute(SYSCLK_2458, (unsigned int)PIT_HZ, &s) == 0);
    CHECK(s.reload == RELOAD_2458);
    CHECK(s.period_us == PERIOD_100HZ);

    /* 2. **直す前の姿**: 1.9968MHz 決め打ちで割った 19968 を 2.4576MHz の
     *    機械に積むと、1 tick は 8125µs = 123Hz。tick を数える待ち・番犬・
     *    CPU 校正が全部 23% 速くなる。 */
    before = (RELOAD_1997 * PIT_PERIOD_SCALE) / (SYSCLK_2458 / PIT_CLOCK_DIV);
    CHECK(before == 8125UL);
    CHECK(before != PERIOD_100HZ);
    /* ずれは 18.75% (10000 → 8125)。丸め誤差ではない。 */
    CHECK((PERIOD_100HZ - before) * 100UL / PERIOD_100HZ >= 18UL);

    /* 3. **NP21/W (1.9968MHz 設定) では何も変わらない** — 直す前と同じ
     *    19968 が出る。だからエミュレータでは一度も踏めなかった。 */
    CHECK(pit_compute(SYSCLK_1997, (unsigned int)PIT_HZ, &s) == 0);
    CHECK(s.reload == RELOAD_1997);
    CHECK(s.period_us == PERIOD_100HZ);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    failed = 0;
    if (!strcmp(argv[1], "selftest_setup")) selftest_setup();
    else if (!strcmp(argv[1], "both_clocks")) both_clocks();
    else if (!strcmp(argv[1], "reject_hz")) reject_hz();
    else if (!strcmp(argv[1], "reject_clock")) reject_clock();
    else if (!strcmp(argv[1], "no_overflow")) no_overflow();
    else if (!strcmp(argv[1], "real_hw_story")) real_hw_story();
    else return 2;
    if (failed) return 1;
    printf("PASS %s\n", argv[1]);
    return 0;
}
