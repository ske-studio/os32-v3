/* ======================================================================== */
/*  LIBOS32MATH.H — OS32 整数数学ライブラリ 公開ヘッダ                      */
/*                                                                          */
/*  FPUを使わず、固定小数点演算・LUT・CORDIC・ニュートン法で                */
/*  ゲーム開発に必要な数学関数を提供する。                                   */
/*  KernelAPI への依存なし。純粋な C89 整数演算のみで構成。                  */
/* ======================================================================== */

#ifndef LIBOS32MATH_H
#define LIBOS32MATH_H

#include "os32_kapi_shared.h"    /* u8, u16, u32, i8, i16, i32 */

/* ====================================================================== */
/*  1. Q16.16 固定小数点型                                                 */
/* ====================================================================== */

typedef i32  fix16_t;

/* 定数 */
#define FIX16_ONE       65536       /* 1.0 */
#define FIX16_HALF      32768       /* 0.5 */
#define FIX16_PI        205887      /* pi  (3.14159 * 65536) */
#define FIX16_2PI       411775      /* 2pi */
#define FIX16_HALF_PI   102944      /* pi/2 */

/* 変換マクロ */
#define FIX16_FROM_INT(x)   ((fix16_t)(x) << 16)
#define FIX16_TO_INT(x)     ((x) >> 16)
#define FIX16_FRAC(x)       ((x) & 0xFFFF)

/* ====================================================================== */
/*  2. sin/cos LUT 定数                                                    */
/* ====================================================================== */

#define ISIN_SCALE      32767       /* sin/cos の 1.0 に相当する値 */
#define ISIN_ENTRIES    512         /* テーブルエントリ数 (360 = 512) */

/* ====================================================================== */
/*  3. 固定小数点演算 (fix16.c)                                            */
/* ====================================================================== */

fix16_t fix16_mul(fix16_t a, fix16_t b);
fix16_t fix16_div(fix16_t a, fix16_t b);
fix16_t fix16_from_frac(int num, int den);
fix16_t fix16_abs(fix16_t x);
fix16_t fix16_ceil(fix16_t x);
fix16_t fix16_floor(fix16_t x);
fix16_t fix16_round(fix16_t x);
fix16_t fix16_min(fix16_t a, fix16_t b);
fix16_t fix16_max(fix16_t a, fix16_t b);
fix16_t fix16_clamp(fix16_t x, fix16_t lo, fix16_t hi);

/* ====================================================================== */
/*  4. 三角関数 (trig.c)                                                   */
/* ====================================================================== */

i32 isin(int angle);              /* 整数sin (512分割, 値域 -32767~+32767) */
i32 icos(int angle);              /* 整数cos (同上) */
int deg_to_idx(int deg);          /* 度数 -> LUTインデックス変換 */
int rad256_to_idx(int rad256);    /* 256分割角度 -> 512分割変換 */

/* ====================================================================== */
/*  5. 平方根 (sqrt.c)                                                     */
/* ====================================================================== */

u32     isqrt(u32 n);                    /* 整数平方根 (ニュートン法) */
fix16_t fix16_sqrt(fix16_t x);           /* Q16.16 平方根 */
u32     fast_distance(int dx, int dy);   /* 2点間距離の高速近似 */
u32     fast_distance_sq(int dx, int dy);/* 距離の二乗 (sqrtなし比較用) */

/* ====================================================================== */
/*  6. atan2 (atan2.c)                                                     */
/* ====================================================================== */

int iatan2(i32 y, i32 x);               /* 整数atan2 (戻り値: 0~511) */
int angle_between(int x0, int y0, int x1, int y1); /* 2点間角度 */

/* ====================================================================== */
/*  7. 逆数テーブル (recip.c)                                              */
/* ====================================================================== */

fix16_t fast_div(fix16_t a, int b);      /* 逆数LUTによる高速除算 (b=1~256) */

/* ====================================================================== */
/*  8. 擬似乱数 (random.c)                                                */
/* ====================================================================== */

void    rng_seed(u32 seed);
u32     rng_next(void);                  /* 次の乱数値 (0~0xFFFFFFFF) */
int     rng_range(int min, int max);     /* 範囲指定乱数 [min, max] */
fix16_t rng_fix16(void);                 /* 0.0~1.0 の Q16.16 乱数 */

/* ====================================================================== */
/*  9. 2Dベクトル (vec2.c)                                                 */
/* ====================================================================== */

typedef struct {
    fix16_t x, y;
} Vec2;

Vec2    vec2_make(fix16_t x, fix16_t y);
Vec2    vec2_add(Vec2 a, Vec2 b);
Vec2    vec2_sub(Vec2 a, Vec2 b);
Vec2    vec2_scale(Vec2 v, fix16_t s);
fix16_t vec2_dot(Vec2 a, Vec2 b);
fix16_t vec2_cross(Vec2 a, Vec2 b);
fix16_t vec2_length_sq(Vec2 v);
fix16_t vec2_length(Vec2 v);
Vec2    vec2_normalize(Vec2 v);
Vec2    vec2_rotate(Vec2 v, int angle);
fix16_t vec2_distance(Vec2 a, Vec2 b);
Vec2    vec2_lerp(Vec2 a, Vec2 b, fix16_t t);

/* ====================================================================== */
/*  10. 線形補間・イージング (lerp.c)                                      */
/* ====================================================================== */

fix16_t fix16_lerp(fix16_t a, fix16_t b, fix16_t t);
int     lerp_int(int a, int b, int t, int t_max);
fix16_t ease_in_quad(fix16_t t);
fix16_t ease_out_quad(fix16_t t);
fix16_t ease_in_out_quad(fix16_t t);
fix16_t ease_in_cubic(fix16_t t);
fix16_t ease_out_cubic(fix16_t t);
fix16_t ease_in_out_cubic(fix16_t t);
fix16_t ease_bounce(fix16_t t);

#endif /* LIBOS32MATH_H */
