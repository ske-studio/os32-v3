/* ======================================================================== */
/*  MATH_TEST.C — libos32math テストプログラム                               */
/*                                                                          */
/*  全モジュール (fix16, trig, sqrt, atan2, recip, random, vec2, lerp) の    */
/*  基本動作を検証するテストスイート。                                        */
/* ======================================================================== */

#include "os32api.h"
#include "libos32math.h"
#include "rt/testresult.h"

extern KernelAPI *kapi;
#define api kapi

/* ---- ヘルパー ---- */

static int g_total = 0;
static int g_passed = 0;

static void check(const char *label, int cond)
{
    g_total++;
    if (cond) {
        g_passed++;
        api->kprintf(ATTR_GREEN, "  [OK] %s\n", label);
    } else {
        api->kprintf(ATTR_RED, "  [NG] %s\n", label);
    }
}

static void check_eq(const char *label, i32 got, i32 expect)
{
    g_total++;
    if (got == expect) {
        g_passed++;
        api->kprintf(ATTR_GREEN, "  [OK] %s = %ld\n", label, (long)got);
    } else {
        api->kprintf(ATTR_RED, "  [NG] %s: got %ld, expect %ld\n",
                     label, (long)got, (long)expect);
    }
}

static void check_range(const char *label, i32 got, i32 lo, i32 hi)
{
    g_total++;
    if (got >= lo && got <= hi) {
        g_passed++;
        api->kprintf(ATTR_GREEN, "  [OK] %s = %ld (in [%ld..%ld])\n",
                     label, (long)got, (long)lo, (long)hi);
    } else {
        api->kprintf(ATTR_RED, "  [NG] %s = %ld (expect [%ld..%ld])\n",
                     label, (long)got, (long)lo, (long)hi);
    }
}

static void header(const char *title)
{
    api->kprintf(ATTR_CYAN, "\n=== %s ===\n", title);
}

/* ======================================================================== */
/*  Test 1: fix16 — Q16.16 固定小数点演算                                   */
/* ======================================================================== */
static void test_fix16(void)
{
    fix16_t a, b, r;

    header("Test 1: fix16");

    /* 基本変換 */
    check_eq("FROM_INT(3)", FIX16_FROM_INT(3), 196608);
    check_eq("TO_INT(196608)", FIX16_TO_INT(196608), 3);

    /* 乗算: 2.0 * 3.0 = 6.0 */
    a = FIX16_FROM_INT(2);
    b = FIX16_FROM_INT(3);
    r = fix16_mul(a, b);
    check_eq("2.0 * 3.0", FIX16_TO_INT(r), 6);

    /* 乗算: 1.5 * 2.0 = 3.0 */
    a = FIX16_FROM_INT(1) + FIX16_HALF;  /* 1.5 */
    b = FIX16_FROM_INT(2);
    r = fix16_mul(a, b);
    check_eq("1.5 * 2.0", FIX16_TO_INT(r), 3);

    /* 除算: 10.0 / 4.0 = 2.5 */
    a = FIX16_FROM_INT(10);
    b = FIX16_FROM_INT(4);
    r = fix16_div(a, b);
    check_eq("10 / 4 (int part)", FIX16_TO_INT(r), 2);
    check_eq("10 / 4 (frac)", FIX16_FRAC(r), FIX16_HALF);

    /* from_frac: 1/3 */
    r = fix16_from_frac(1, 3);
    check_range("1/3", r, 21844, 21846);  /* 65536/3 = 21845.33 */

    /* abs */
    check_eq("abs(-5)", FIX16_TO_INT(fix16_abs(FIX16_FROM_INT(-5))), 5);
    check_eq("abs(5)", FIX16_TO_INT(fix16_abs(FIX16_FROM_INT(5))), 5);

    /* ceil, floor, round */
    a = FIX16_FROM_INT(2) + FIX16_HALF;  /* 2.5 */
    check_eq("ceil(2.5)", FIX16_TO_INT(fix16_ceil(a)), 3);
    check_eq("floor(2.5)", FIX16_TO_INT(fix16_floor(a)), 2);
    check_eq("round(2.5)", FIX16_TO_INT(fix16_round(a)), 3);

    a = FIX16_FROM_INT(2) + 16384;  /* 2.25 */
    check_eq("round(2.25)", FIX16_TO_INT(fix16_round(a)), 2);

    /* clamp */
    r = fix16_clamp(FIX16_FROM_INT(5), FIX16_FROM_INT(0), FIX16_FROM_INT(3));
    check_eq("clamp(5, 0, 3)", FIX16_TO_INT(r), 3);
    r = fix16_clamp(FIX16_FROM_INT(-1), FIX16_FROM_INT(0), FIX16_FROM_INT(3));
    check_eq("clamp(-1, 0, 3)", FIX16_TO_INT(r), 0);
    r = fix16_clamp(FIX16_FROM_INT(2), FIX16_FROM_INT(0), FIX16_FROM_INT(3));
    check_eq("clamp(2, 0, 3)", FIX16_TO_INT(r), 2);

    /* min, max */
    check_eq("min(3, 5)", FIX16_TO_INT(fix16_min(FIX16_FROM_INT(3), FIX16_FROM_INT(5))), 3);
    check_eq("max(3, 5)", FIX16_TO_INT(fix16_max(FIX16_FROM_INT(3), FIX16_FROM_INT(5))), 5);

    /* ゼロ除算保護 */
    check_eq("div by 0", fix16_div(FIX16_FROM_INT(5), 0), 0);
}

/* ======================================================================== */
/*  Test 2: trig — sin/cos LUT                                              */
/* ======================================================================== */
static void test_trig(void)
{
    header("Test 2: trig");

    /* sin(0) = 0 */
    check_eq("isin(0)", isin(0), 0);

    /* sin(128) = sin(90deg) = 32767 */
    check_eq("isin(128) = sin(90)", isin(128), 32767);

    /* sin(256) = sin(180deg) = 0 */
    check_eq("isin(256) = sin(180)", isin(256), 0);

    /* sin(384) = sin(270deg) = -32767 */
    check_eq("isin(384) = sin(270)", isin(384), -32767);

    /* cos(0) = 32767 */
    check_eq("icos(0) = cos(0)", icos(0), 32767);

    /* cos(128) = cos(90deg) = 0 */
    check_eq("icos(128) = cos(90)", icos(128), 0);

    /* cos(256) = cos(180deg) = -32767 */
    check_eq("icos(256) = cos(180)", icos(256), -32767);

    /* 自動正規化: sin(512) = sin(0) = 0 */
    check_eq("isin(512) wrap", isin(512), 0);
    check_eq("isin(-128) wrap", isin(-128), isin(384));

    /* deg_to_idx */
    check_eq("deg_to_idx(0)", deg_to_idx(0), 0);
    check_range("deg_to_idx(90)", deg_to_idx(90), 127, 129);
    check_range("deg_to_idx(180)", deg_to_idx(180), 255, 257);
    check_range("deg_to_idx(360)", deg_to_idx(360), 0, 0);

    /* rad256_to_idx */
    check_eq("rad256_to_idx(0)", rad256_to_idx(0), 0);
    check_eq("rad256_to_idx(64)", rad256_to_idx(64), 128);
    check_eq("rad256_to_idx(128)", rad256_to_idx(128), 256);
}

/* ======================================================================== */
/*  Test 3: sqrt — 整数平方根                                               */
/* ======================================================================== */
static void test_sqrt(void)
{
    fix16_t r;

    header("Test 3: sqrt");

    check_eq("isqrt(0)", (i32)isqrt(0), 0);
    check_eq("isqrt(1)", (i32)isqrt(1), 1);
    check_eq("isqrt(4)", (i32)isqrt(4), 2);
    check_eq("isqrt(9)", (i32)isqrt(9), 3);
    check_eq("isqrt(100)", (i32)isqrt(100), 10);
    check_eq("isqrt(10000)", (i32)isqrt(10000), 100);
    check_eq("isqrt(65536)", (i32)isqrt(65536), 256);

    /* floor(sqrt(2)) = 1 */
    check_eq("isqrt(2)", (i32)isqrt(2), 1);
    /* floor(sqrt(8)) = 2 */
    check_eq("isqrt(8)", (i32)isqrt(8), 2);

    /* fix16_sqrt(4.0) = 2.0 */
    r = fix16_sqrt(FIX16_FROM_INT(4));
    check_eq("fix16_sqrt(4.0) int", FIX16_TO_INT(r), 2);

    /* fix16_sqrt(1.0) = 1.0 */
    r = fix16_sqrt(FIX16_ONE);
    check_range("fix16_sqrt(1.0)", r, FIX16_ONE - 512, FIX16_ONE + 512);

    /* fast_distance_sq */
    check_eq("dist_sq(3,4)", (i32)fast_distance_sq(3, 4), 25);
    check_eq("dist_sq(0,0)", (i32)fast_distance_sq(0, 0), 0);

    /* fast_distance: 近似なので誤差許容 */
    check_range("fast_dist(3,4)", (i32)fast_distance(3, 4), 4, 6);
    check_range("fast_dist(10,0)", (i32)fast_distance(10, 0), 10, 10);
}

/* ======================================================================== */
/*  Test 4: atan2 — CORDIC iatan2                                           */
/* ======================================================================== */
static void test_atan2(void)
{
    int a;

    header("Test 4: atan2");

    /* 右方向 (1, 0) = 0 */
    a = iatan2(0, 100);
    check_eq("atan2(0,100) = 0", a, 0);

    /* 下方向 (0, 1) = 128 (90deg) */
    a = iatan2(100, 0);
    check_range("atan2(100,0) ~ 128", a, 125, 131);

    /* 左方向 (-1, 0) = 256 (180deg) */
    a = iatan2(0, -100);
    check_range("atan2(0,-100) ~ 256", a, 253, 259);

    /* 上方向 (0, -1) = 384 (270deg) */
    a = iatan2(-100, 0);
    check_range("atan2(-100,0) ~ 384", a, 381, 387);

    /* 45度 (1, 1) = 64 */
    a = iatan2(100, 100);
    check_range("atan2(100,100) ~ 64", a, 60, 68);

    /* 原点 */
    check_eq("atan2(0,0)", iatan2(0, 0), 0);

    /* angle_between (CORDIC誤差許容) */
    a = angle_between(0, 0, 10, 0);
    check_range("angle_between right", a, 0, 5);
}

/* ======================================================================== */
/*  Test 5: recip — 逆数テーブル高速除算                                     */
/* ======================================================================== */
static void test_recip(void)
{
    fix16_t r;

    header("Test 5: recip");

    /* 10.0 / 1 = 10.0 */
    r = fast_div(FIX16_FROM_INT(10), 1);
    check_eq("10/1", FIX16_TO_INT(r), 10);

    /* 10.0 / 2 = 5.0 */
    r = fast_div(FIX16_FROM_INT(10), 2);
    check_eq("10/2", FIX16_TO_INT(r), 5);

    /* 10.0 / 4 = 2.5 → int part = 2 */
    r = fast_div(FIX16_FROM_INT(10), 4);
    check_eq("10/4 int", FIX16_TO_INT(r), 2);

    /* 100.0 / 10 = 10.0 */
    r = fast_div(FIX16_FROM_INT(100), 10);
    check_eq("100/10", FIX16_TO_INT(r), 10);

    /* 256.0 / 256 = 1.0 */
    r = fast_div(FIX16_FROM_INT(256), 256);
    check_eq("256/256", FIX16_TO_INT(r), 1);
}

/* ======================================================================== */
/*  Test 6: random — xorshift32                                              */
/* ======================================================================== */
static void test_random(void)
{
    u32 v1, v2;
    int r;
    int i;
    int in_range;
    fix16_t f;

    header("Test 6: random");

    /* シード設定後の再現性 */
    rng_seed(12345);
    v1 = rng_next();
    rng_seed(12345);
    v2 = rng_next();
    check_eq("reproducible", (i32)v1, (i32)v2);

    /* 連続生成が異なる値を返す */
    v1 = rng_next();
    v2 = rng_next();
    check("consecutive differ", v1 != v2);

    /* rng_range: 100回試行して全て範囲内 */
    rng_seed(42);
    in_range = 1;
    for (i = 0; i < 100; i++) {
        r = rng_range(10, 20);
        if (r < 10 || r > 20) {
            in_range = 0;
            break;
        }
    }
    check("rng_range [10,20] x100", in_range);

    /* rng_range: min == max */
    check_eq("rng_range(5,5)", rng_range(5, 5), 5);

    /* rng_fix16: 0 ~ 65535 */
    rng_seed(99);
    f = rng_fix16();
    check("rng_fix16 in [0, 65535]", f >= 0 && f <= 65535);

    /* ゼロシード保護 */
    rng_seed(0);
    v1 = rng_next();
    check("seed(0) != 0 output", v1 != 0);
}

/* ======================================================================== */
/*  Test 7: vec2 — 2Dベクトル演算                                           */
/* ======================================================================== */
static void test_vec2(void)
{
    Vec2 a, b, r;
    fix16_t d;

    header("Test 7: vec2");

    a = vec2_make(FIX16_FROM_INT(3), FIX16_FROM_INT(4));
    check_eq("make.x", FIX16_TO_INT(a.x), 3);
    check_eq("make.y", FIX16_TO_INT(a.y), 4);

    /* add */
    b = vec2_make(FIX16_FROM_INT(1), FIX16_FROM_INT(2));
    r = vec2_add(a, b);
    check_eq("add.x", FIX16_TO_INT(r.x), 4);
    check_eq("add.y", FIX16_TO_INT(r.y), 6);

    /* sub */
    r = vec2_sub(a, b);
    check_eq("sub.x", FIX16_TO_INT(r.x), 2);
    check_eq("sub.y", FIX16_TO_INT(r.y), 2);

    /* scale */
    r = vec2_scale(a, FIX16_FROM_INT(2));
    check_eq("scale.x", FIX16_TO_INT(r.x), 6);
    check_eq("scale.y", FIX16_TO_INT(r.y), 8);

    /* dot: (3,4).(1,2) = 3+8 = 11 */
    d = vec2_dot(a, b);
    check_eq("dot", FIX16_TO_INT(d), 11);

    /* cross: (3,4)x(1,2) = 3*2 - 4*1 = 2 */
    d = vec2_cross(a, b);
    check_eq("cross", FIX16_TO_INT(d), 2);

    /* length_sq: (3,4) -> 9+16 = 25 */
    d = vec2_length_sq(a);
    check_eq("length_sq", FIX16_TO_INT(d), 25);

    /* length: (3,4) -> 5 */
    d = vec2_length(a);
    check_range("length(3,4)", FIX16_TO_INT(d), 4, 5);

    /* normalize: ゼロベクトル */
    r = vec2_normalize(vec2_make(0, 0));
    check_eq("normalize(0,0).x", r.x, 0);
    check_eq("normalize(0,0).y", r.y, 0);

    /* distance: (0,0) -> (3,4) = 5 */
    a = vec2_make(0, 0);
    b = vec2_make(FIX16_FROM_INT(3), FIX16_FROM_INT(4));
    d = vec2_distance(a, b);
    check_range("distance(0,0)-(3,4)", FIX16_TO_INT(d), 4, 5);

    /* lerp: t=0 -> a, t=1 -> b */
    a = vec2_make(FIX16_FROM_INT(0), FIX16_FROM_INT(0));
    b = vec2_make(FIX16_FROM_INT(10), FIX16_FROM_INT(20));
    r = vec2_lerp(a, b, 0);
    check_eq("lerp t=0 x", FIX16_TO_INT(r.x), 0);
    r = vec2_lerp(a, b, FIX16_ONE);
    check_eq("lerp t=1 x", FIX16_TO_INT(r.x), 10);
    check_eq("lerp t=1 y", FIX16_TO_INT(r.y), 20);
    r = vec2_lerp(a, b, FIX16_HALF);
    check_eq("lerp t=0.5 x", FIX16_TO_INT(r.x), 5);

    /* rotate: (1,0) by 128 (90deg) -> (0,1) 近似 */
    a = vec2_make(FIX16_FROM_INT(10), 0);
    r = vec2_rotate(a, 128);
    check_range("rotate 90 x", FIX16_TO_INT(r.x), -1, 1);
    check_range("rotate 90 y", FIX16_TO_INT(r.y), 9, 11);
}

/* ======================================================================== */
/*  Test 8: lerp / easing                                                    */
/* ======================================================================== */
static void test_lerp(void)
{
    fix16_t r;

    header("Test 8: lerp / easing");

    /* fix16_lerp */
    r = fix16_lerp(FIX16_FROM_INT(0), FIX16_FROM_INT(100), FIX16_HALF);
    check_eq("lerp(0,100,0.5)", FIX16_TO_INT(r), 50);

    r = fix16_lerp(FIX16_FROM_INT(10), FIX16_FROM_INT(20), 0);
    check_eq("lerp t=0", FIX16_TO_INT(r), 10);

    r = fix16_lerp(FIX16_FROM_INT(10), FIX16_FROM_INT(20), FIX16_ONE);
    check_eq("lerp t=1", FIX16_TO_INT(r), 20);

    /* lerp_int */
    check_eq("lerp_int(0,100,50,100)", lerp_int(0, 100, 50, 100), 50);
    check_eq("lerp_int t=0", lerp_int(10, 20, 0, 100), 10);
    check_eq("lerp_int t=max", lerp_int(10, 20, 100, 100), 20);

    /* イージング: t=0 -> 0, t=1 -> 1 の境界条件 */
    check_eq("ease_in_quad(0)", ease_in_quad(0), 0);
    check_range("ease_in_quad(1)", ease_in_quad(FIX16_ONE),
                FIX16_ONE - 2, FIX16_ONE + 2);

    check_eq("ease_out_quad(0)", ease_out_quad(0), 0);
    check_range("ease_out_quad(1)", ease_out_quad(FIX16_ONE),
                FIX16_ONE - 2, FIX16_ONE + 2);

    check_eq("ease_in_out_quad(0)", ease_in_out_quad(0), 0);
    check_range("ease_in_out_quad(1)", ease_in_out_quad(FIX16_ONE),
                FIX16_ONE - 2, FIX16_ONE + 2);

    check_eq("ease_in_cubic(0)", ease_in_cubic(0), 0);
    check_range("ease_in_cubic(1)", ease_in_cubic(FIX16_ONE),
                FIX16_ONE - 2, FIX16_ONE + 2);

    check_eq("ease_out_cubic(0)", ease_out_cubic(0), 0);
    check_range("ease_out_cubic(1)", ease_out_cubic(FIX16_ONE),
                FIX16_ONE - 2, FIX16_ONE + 2);

    check_eq("ease_in_out_cubic(0)", ease_in_out_cubic(0), 0);
    check_range("ease_in_out_cubic(1)", ease_in_out_cubic(FIX16_ONE),
                FIX16_ONE - 2, FIX16_ONE + 2);

    /* bounce: t=0 -> 0 */
    check_eq("ease_bounce(0)", ease_bounce(0), 0);
    /* bounce: t=1 -> ~1.0 */
    check_range("ease_bounce(1)", ease_bounce(FIX16_ONE),
                FIX16_ONE - 2048, FIX16_ONE + 2048);

    /* 単調性: ease_in_quad は t が増えるほど値が増える */
    {
        fix16_t v1 = ease_in_quad(FIX16_FROM_INT(0));
        fix16_t v2 = ease_in_quad(FIX16_HALF);
        fix16_t v3 = ease_in_quad(FIX16_ONE);
        check("ease_in_quad monotonic", v1 <= v2 && v2 <= v3);
    }
}

/* ======================================================================== */
/*  エントリポイント                                                         */
/* ======================================================================== */
int main(int argc, char **argv, KernelAPI *k)
{
    (void)argc; (void)argv; (void)k;

    api->kprintf(ATTR_CYAN, "math_test: libos32math test suite\n");

    test_fix16();
    test_trig();
    test_sqrt();
    test_atan2();
    test_recip();
    test_random();
    test_vec2();
    test_lerp();

    /* サマリ */
    return os32_test_summary(api, "math_test", g_passed, g_total);
}
