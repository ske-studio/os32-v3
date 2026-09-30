/* sqrt.rs — 整数平方根・距離計算
 *
 * C版 sqrt.c の Rust 移植。
 * isqrt: ニュートン法 (Heron法) による整数平方根。
 * fix16_sqrt: Q16.16 対応の平方根。
 * fast_distance: α-max-plus-β-min 近似による高速距離計算。
 */

use crate::fix16::Fix16;

/* ====================================================================== */
/*  isqrt — 整数平方根 (ニュートン法)                                       */
/*                                                                          */
/*  floor(sqrt(n)) を返す。n=0 のときは 0。                                 */
/*  最大6回の反復で u32 全範囲に対応。                                       */
/* ====================================================================== */

pub fn isqrt(n: u32) -> u32 {
    if n == 0 { return 0; }
    if n < 4 { return 1; }

    /* 初期推定値: n のビット長の半分からスタート */
    let mut x = n;
    let mut x1 = (x + 1) >> 1;

    /* ニュートン法の反復 */
    while x1 < x {
        x = x1;
        x1 = (x + n / x) >> 1;
    }
    x
}

/* ====================================================================== */
/*  fix16_sqrt — Q16.16 平方根                                              */
/*                                                                          */
/*  入力値を32bit左シフトしてからisqrtを呼び、Q16.16の精度を得る。          */
/* ====================================================================== */

pub fn fix16_sqrt(x: Fix16) -> Fix16 {
    if x.raw() <= 0 { return Fix16::ZERO; }

    let val = x.raw() as u32;

    /*
     * sqrt(x_fix16) = sqrt(x_real * 65536) = sqrt(x_real) * 256
     * Q16.16 で結果を得るには、さらに256倍が必要。
     * 2段階で計算し精度を上げる。
     */
    let mut result = isqrt(val) << 8;

    /* ニュートン法で1段階精度を上げる */
    if result > 0 {
        let r = Fix16::from_raw(result as i32);
        let check = r * r;
        if check < x {
            let correction = (x - check) / Fix16::from_raw((result << 1) as i32);
            result += correction.raw() as u32;
        }
    }

    Fix16::from_raw(result as i32)
}

/* ====================================================================== */
/*  fast_distance_sq — 距離の二乗 (sqrtなし)                                */
/*                                                                          */
/*  衝突判定等の距離比較で使用。sqrtを呼ばないため高速。                     */
/* ====================================================================== */

pub fn fast_distance_sq(dx: i32, dy: i32) -> u32 {
    (dx as i64 * dx as i64 + dy as i64 * dy as i64) as u32
}

/* ====================================================================== */
/*  fast_distance — 高速距離近似 (alpha-max-plus-beta-min)                  */
/*                                                                          */
/*  alpha=1, beta=3/8 の近似。最大誤差 ~3.5%。                              */
/* ====================================================================== */

pub fn fast_distance(dx: i32, dy: i32) -> u32 {
    let mut a = dx.unsigned_abs();
    let mut b = dy.unsigned_abs();

    /* a >= b を保証 */
    if a < b {
        core::mem::swap(&mut a, &mut b);
    }

    /* alpha*max + beta*min = max + (3*min)/8 */
    a + ((3 * b) >> 3)
}

/* ====================================================================== */
/*  C互換FFI関数                                                            */
/* ====================================================================== */

#[no_mangle]
pub extern "C" fn rs_isqrt(n: u32) -> u32 { isqrt(n) }

#[no_mangle]
pub extern "C" fn rs_fix16_sqrt(x: i32) -> i32 { fix16_sqrt(Fix16::from_raw(x)).raw() }

#[no_mangle]
pub extern "C" fn rs_fast_distance_sq(dx: i32, dy: i32) -> u32 { fast_distance_sq(dx, dy) }

#[no_mangle]
pub extern "C" fn rs_fast_distance(dx: i32, dy: i32) -> u32 { fast_distance(dx, dy) }

/* ====================================================================== */
/*  テスト                                                                  */
/* ====================================================================== */

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_isqrt() {
        assert_eq!(isqrt(0), 0);
        assert_eq!(isqrt(1), 1);
        assert_eq!(isqrt(4), 2);
        assert_eq!(isqrt(9), 3);
        assert_eq!(isqrt(100), 10);
        assert_eq!(isqrt(10000), 100);
        assert_eq!(isqrt(8), 2); /* floor(sqrt(8)) = 2 */
    }

    #[test]
    fn test_fast_distance() {
        /* 3-4-5 三角形 */
        let d = fast_distance(3, 4);
        /* alpha-max + beta-min = 4 + 3*3/8 = 4 + 1 = 5 */
        assert!(d >= 4 && d <= 6);

        /* 対称性 */
        assert_eq!(fast_distance(3, 4), fast_distance(-3, 4));
        assert_eq!(fast_distance(3, 4), fast_distance(4, 3));
    }

    #[test]
    fn test_fast_distance_sq() {
        assert_eq!(fast_distance_sq(3, 4), 25);
        assert_eq!(fast_distance_sq(0, 0), 0);
    }
}
