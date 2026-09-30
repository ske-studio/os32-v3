/* ======================================================================== */
/*  TIME_MATH_HOST.C — kernel/time_math.c をそのままホストで回す            */
/*                                                                          */
/*  実物を 1 行も写さずに #include する。PIT も PIC も触らないので模型は     */
/*  1 つも要らない。                                                        */
/*                                                                          */
/*  見るのは **実機でも NP21/W でも撃ち分けられない**分岐                   */
/*  (記録: time_math_tdd.md):                                               */
/*    (a) p1/p2 の判定表 3 分岐 (呼び出しから p1 読みまでに境界を越える      */
/*        機械があり、位相待ちでは 0/0 と 0/1 を作り分けられない)           */
/*    (b) 両方のクロック (reload 19968 / 24576) で周期がちょうど 10000µs     */
/*    (c) **71 分の桁あふれ** — u32 で組むと 429496 tick で時刻が戻る。      */
/*        起動から 71 分は NP21/W でも実機でも 1 度も回していない            */
/* ======================================================================== */
#include <stdio.h>
#include <string.h>
#include "../../kernel/time_math.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* **式ではなく数で書く** — 実装と同じ式で期待値を作ると両方が同じだけ
 * ずれたときに気づけない (pit_clock_host.c と同じ流儀)。 */
#define RELOAD_1997   19968U
#define RELOAD_2458   24576U
#define PERIOD_100HZ  10000U

/* ------------------------------------------------------------------ */
/*  (a) p1 / p2 の判定表                                               */
/* ------------------------------------------------------------------ */
static void decide_table(void)
{
    struct time_snapshot s;

    time_branch_reset();

    /* 境界を踏んでいない: t と count をそのまま */
    memset(&s, 0xAA, sizeof(s));
    CHECK(time_decide(0, 0, 100, 5000, &s) == TIME_DECIDE_OK);
    CHECK(s.tick == 100);
    CHECK(s.count == 5000);

    /* **ラッチより前に再ロードが済んでいる**: tick は t + 1。
     * ここを t のままにすると、新しい周期の count (= reload に近い大きな値)
     * を古い tick と組ませ、時刻が 1 周期ぶん戻る (往復 2 の反例 1)。 */
    memset(&s, 0xAA, sizeof(s));
    CHECK(time_decide(1, 0, 100, 19900, &s) == TIME_DECIDE_OK);
    CHECK(s.tick == 101);
    CHECK(s.count == 19900);

    /* p1 = 1 なら p2 は見ない (どちらでも t + 1) */
    CHECK(time_decide(1, 1, 7, 42, &s) == TIME_DECIDE_OK);
    CHECK(s.tick == 8);

    /* **前後どちらか不明**: 採用しない。どちらへ寄せても最大 1 周期ずれる。 */
    memset(&s, 0x5A, sizeof(s));
    CHECK(time_decide(0, 1, 100, 19900, &s) == TIME_DECIDE_RETRY);
    /* やり直しのときは out を触らない (呼び手が古い値を本物と読まない) */
    CHECK(s.tick == 0x5A5A5A5AU);

    /* 3 分岐を全部踏んだことを数で確かめる (W4 の網羅はこの数で見る) */
    CHECK(time_branch_hits[TIME_BR_T0] == 1);
    CHECK(time_branch_hits[TIME_BR_T1] == 2);
    CHECK(time_branch_hits[TIME_BR_RETRY] == 1);

    CHECK(time_decide(0, 0, 1, 1, 0) == TIME_DECIDE_RETRY);   /* NULL は採らない */
}

/* ------------------------------------------------------------------ */
/*  (b) 補間の算数 — 両方のクロック                                     */
/* ------------------------------------------------------------------ */
static void us_math(void)
{
    unsigned int lo, hi;

    /* 周期のはじめ (count == reload): 端数 0 */
    time_us_from(0, RELOAD_1997, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
    CHECK(lo == 0 && hi == 0);
    time_us_from(3, RELOAD_2458, RELOAD_2458, PERIOD_100HZ, &lo, &hi);
    CHECK(lo == 30000 && hi == 0);

    /* 真ん中 (どちらのクロックでも 5000µs) */
    time_us_from(0, RELOAD_1997 / 2, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
    CHECK(lo == 5000 && hi == 0);
    time_us_from(0, RELOAD_2458 / 2, RELOAD_2458, PERIOD_100HZ, &lo, &hi);
    CHECK(lo == 5000 && hi == 0);

    /* 周期の終わり (count == 1): 端数は period 未満のまま */
    time_us_from(0, 1, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
    CHECK(lo == 9999 && hi == 0);
    time_us_from(0, 1, RELOAD_2458, PERIOD_100HZ, &lo, &hi);
    CHECK(lo == 9999 && hi == 0);

    /* count == 0 は 1 周期ぶん。**tick と足して次の tick を越えない**
     * (越えると単調性が壊れる) — ちょうど period になる。 */
    time_us_from(0, 0, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
    CHECK(lo == PERIOD_100HZ && hi == 0);

    /* 100 tick = 1 秒 */
    time_us_from(100, RELOAD_1997, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
    CHECK(lo == 1000000 && hi == 0);

    /* reload = 0 (PIT 未初期化の値が漏れた) でも 0 除算しない */
    time_us_from(5, 123, 0, PERIOD_100HZ, &lo, &hi);
    CHECK(lo == 50000 && hi == 0);

    /* 範囲外の count でも巻き込まない (注入試験が渡し得る) */
    time_us_from(0, RELOAD_1997 + 100, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
    CHECK(lo == 0 && hi == 0);

    /* NULL は書かないだけ */
    time_us_from(1, 1, RELOAD_1997, PERIOD_100HZ, 0, 0);
}

/* ------------------------------------------------------------------ */
/*  (c) 71 分の桁あふれ                                                */
/* ------------------------------------------------------------------ */
static void no_u32_overflow(void)
{
    unsigned int lo, hi;
    unsigned long long us;

    /* 2^32 µs = 4294.967296 秒 = 71.58 分。tick にすると 429496.7295。
     * **u32 で組むとここで時刻が 0 に戻る。** */
    CHECK(429496U * PERIOD_100HZ < 0xFFFFFFFFU);      /* 直前はまだ収まる */
    time_us_from(429496U, RELOAD_1997, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
    CHECK(hi == 0);
    CHECK(lo == 4294960000U);

    /* 1 tick 先で溢れる */
    time_us_from(429497U, RELOAD_1997, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
    CHECK(hi == 1);
    CHECK(lo == 4294970000U - 4294967296U);           /* = 2704 */

    /* 上下を組み直すと素直な値になる */
    us = ((unsigned long long)hi << 32) | lo;
    CHECK(us == 4294970000ULL);

    /* さらに先 (497 日ではなく 1 日ぶん) でも単調 */
    time_us_from(8640000U, RELOAD_1997, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
    us = ((unsigned long long)hi << 32) | lo;
    CHECK(us == 86400000000ULL);
    CHECK(hi == 20);
}

/* ------------------------------------------------------------------ */
/*  単調性 — tick が進み count が減る筋書きを通す                       */
/* ------------------------------------------------------------------ */
static void monotonic(void)
{
    unsigned long long prev = 0;
    unsigned int tick, count, lo, hi;
    int back = 0;

    for (tick = 0; tick < 300; tick++) {
        /* mode 2 は reload から 1 へ減る */
        for (count = RELOAD_2458; count >= 1; count -= 1024) {
            unsigned long long us;
            time_us_from(tick, count, RELOAD_2458, PERIOD_100HZ, &lo, &hi);
            us = ((unsigned long long)hi << 32) | lo;
            if (us < prev) back++;
            prev = us;
            if (count < 1024) break;
        }
    }
    CHECK(back == 0);
    CHECK(prev > 0);

    /* **境界を跨ぐ組み合わせ**: 周期の終わり (t, count=1) の次は
     * (t+1, count=reload)。ここが逆転すると 1 万回読みで逆行が出る。 */
    {
        unsigned long long a, b;
        time_us_from(50, 1, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
        a = ((unsigned long long)hi << 32) | lo;
        time_us_from(51, RELOAD_1997, RELOAD_1997, PERIOD_100HZ, &lo, &hi);
        b = ((unsigned long long)hi << 32) | lo;
        CHECK(b > a);
    }
}

/* ------------------------------------------------------------------ */
/*  クランプ — p1/p2 をすり抜けた巻き戻りを押さえる                     */
/*                                                                      */
/*  NP21/W は 8254 の再ロードと 8259 の IRR を原子的に模擬しないので、  */
/*  判定表を正しく通しても前回より小さい値が出る (両方の順序が出る)。   */
/*  実機でも数百 ns の窓で同じ形になる。                                */
/* ------------------------------------------------------------------ */
static void clamp(void)
{
    unsigned long long out;

    /* 進んでいる: そのまま通す。数えない。 */
    out = 0xA5A5A5A5A5A5A5A5ULL;
    CHECK(time_clamp(10000, 9999, &out) == 0);
    CHECK(out == 10000);

    /* **等しいのはクランプではない** — 同じ µs を 2 回読むのは正常。 */
    out = 0xA5A5A5A5A5A5A5A5ULL;
    CHECK(time_clamp(10000, 10000, &out) == 0);
    CHECK(out == 10000);

    /* 巻き戻り (a): count が先に再ロードされ IRR がまだ → t*10000 + 小さい端数。
     * 旧周期の終わり 9999 より小さいので、前回値を返す。 */
    out = 0xA5A5A5A5A5A5A5A5ULL;
    CHECK(time_clamp(2, 9999, &out) == 1);
    CHECK(out == 9999);

    /* 巻き戻り (b): IRR が先に立って (t+2) 相当まで跳び、ISR の後で戻る。
     * 跳んだ側は通り、戻った側が押さえられる。 */
    {
        unsigned long long last = 0, v;
        int clamped = 0;
        CHECK(time_clamp(30000, last, &v) == 0);  /* (t+2)*10000 まで跳ぶ */
        last = v;
        clamped += time_clamp(20001, last, &v);   /* (t+1)*10000 + 1 で戻る */
        last = v;
        CHECK(clamped == 1);
        CHECK(last == 30000);
    }

    /* 1 万回の筋書き: 1 回おきに巻き戻る入力でも出力は単調、
     * 数えた回数は巻き戻った回数ちょうど。 */
    {
        unsigned long long last = 0, prev = 0, v;
        unsigned int i;
        int count = 0, back = 0;
        for (i = 0; i < 10000; i++) {
            /* 1 周期 100 µs で進み、奇数回だけ 150 µs 戻る (= 前回より小さい) */
            unsigned long long raw = 1000ULL + (unsigned long long)i * 100;
            if (i & 1) raw -= 150;
            count += time_clamp(raw, last, &v);
            last = v;
            if (v < prev) back++;
            prev = v;
        }
        CHECK(back == 0);
        CHECK(count == 5000);
        CHECK(last == 1000ULL + 9998ULL * 100);   /* 最後の偶数回の値で止まる */
    }

    /* **64 ビットの桁を跨いでも単調** (71 分の境界。u32 に潰すと逆転する) */
    {
        unsigned long long v;
        CHECK(time_clamp(4294970000ULL, 4294960000ULL, &v) == 0);
        CHECK(v == 4294970000ULL);
        CHECK(time_clamp(2704ULL, 4294970000ULL, &v) == 1);  /* 下位だけ見ると大きい */
        CHECK(v == 4294970000ULL);
    }

    /* NULL は書かないだけ。判定は返る。 */
    CHECK(time_clamp(1, 2, 0) == 1);
    CHECK(time_clamp(2, 1, 0) == 0);
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    const char *c = (argc > 1) ? argv[1] : "";

    failed = 0;
    if (!strcmp(c, "decide_table")) decide_table();
    else if (!strcmp(c, "us_math")) us_math();
    else if (!strcmp(c, "no_u32_overflow")) no_u32_overflow();
    else if (!strcmp(c, "monotonic")) monotonic();
    else if (!strcmp(c, "clamp")) clamp();
    else { fprintf(stderr, "unknown case: %s\n", c); return 2; }
    return failed ? 1 : 0;
}
