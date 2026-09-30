/* fix16.rs — Q16.16 固定小数点型と四則演算
 *
 * C版 fix16.c の Rust 移植。
 * 32bit整数の上位16bitを整数部、下位16bitを小数部として扱う。
 * core::ops トレイト実装により、演算子オーバーロードで自然に使える。
 */

use core::fmt;
use core::ops::{Add, Sub, Mul, Div, Neg};

/* 定数 */
pub const ONE: i32 = 65536;       /* 1.0 */
pub const HALF: i32 = 32768;      /* 0.5 */
pub const PI: i32 = 205887;       /* π  (3.14159 * 65536) */
pub const TWO_PI: i32 = 411775;   /* 2π */
pub const HALF_PI: i32 = 102944;  /* π/2 */

/* ====================================================================== */
/*  Fix16 — Q16.16 固定小数点 newtype                                      */
/* ====================================================================== */

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
#[repr(transparent)]
pub struct Fix16(pub i32);

impl Fix16 {
    pub const ZERO: Fix16 = Fix16(0);
    pub const ONE: Fix16 = Fix16(ONE);
    pub const HALF: Fix16 = Fix16(HALF);
    pub const PI: Fix16 = Fix16(PI);
    pub const TWO_PI: Fix16 = Fix16(TWO_PI);
    pub const HALF_PI: Fix16 = Fix16(HALF_PI);

    /* 整数からの変換 */
    #[inline]
    pub const fn from_int(x: i32) -> Self {
        Fix16(x << 16)
    }

    /* 整数部分の取得 */
    #[inline]
    pub const fn to_int(self) -> i32 {
        self.0 >> 16
    }

    /* 小数部分の取得 */
    #[inline]
    pub const fn frac(self) -> i32 {
        self.0 & 0xFFFF
    }

    /* 分数から変換 (num/den) */
    #[inline]
    pub fn from_frac(num: i32, den: i32) -> Self {
        if den == 0 {
            return Fix16(0);
        }
        Fix16((((num as i64) << 16) / den as i64) as i32)
    }

    /* 絶対値 */
    #[inline]
    pub const fn abs(self) -> Self {
        if self.0 < 0 { Fix16(-self.0) } else { self }
    }

    /* 切り上げ */
    #[inline]
    pub const fn ceil(self) -> Self {
        if self.0 & 0xFFFF != 0 {
            Fix16((self.0 & !0xFFFF) + ONE)
        } else {
            self
        }
    }

    /* 切り捨て */
    #[inline]
    pub const fn floor(self) -> Self {
        Fix16(self.0 & !0xFFFF)
    }

    /* 四捨五入 */
    #[inline]
    pub const fn round(self) -> Self {
        Fix16((self.0 + HALF) & !0xFFFF)
    }

    /* 最小値 */
    #[inline]
    pub const fn min(self, other: Self) -> Self {
        if self.0 < other.0 { self } else { other }
    }

    /* 最大値 */
    #[inline]
    pub const fn max(self, other: Self) -> Self {
        if self.0 > other.0 { self } else { other }
    }

    /* クランプ */
    #[inline]
    pub const fn clamp(self, lo: Self, hi: Self) -> Self {
        if self.0 < lo.0 {
            lo
        } else if self.0 > hi.0 {
            hi
        } else {
            self
        }
    }

    /* 内部値の直接取得 */
    #[inline]
    pub const fn raw(self) -> i32 {
        self.0
    }

    /* 内部値からの直接生成 */
    #[inline]
    pub const fn from_raw(raw: i32) -> Self {
        Fix16(raw)
    }
}

/* ====================================================================== */
/*  演算子トレイト実装                                                      */
/* ====================================================================== */

impl Add for Fix16 {
    type Output = Self;
    #[inline]
    fn add(self, rhs: Self) -> Self {
        Fix16(self.0 + rhs.0)
    }
}

impl Sub for Fix16 {
    type Output = Self;
    #[inline]
    fn sub(self, rhs: Self) -> Self {
        Fix16(self.0 - rhs.0)
    }
}

/* Q16.16 乗算: 64bit中間値を使用してオーバーフロー防止 */
impl Mul for Fix16 {
    type Output = Self;
    #[inline]
    fn mul(self, rhs: Self) -> Self {
        Fix16(((self.0 as i64 * rhs.0 as i64) >> 16) as i32)
    }
}

/* Q16.16 除算: (a << 16) / b を64bit演算で計算 */
impl Div for Fix16 {
    type Output = Self;
    #[inline]
    fn div(self, rhs: Self) -> Self {
        if rhs.0 == 0 {
            return Fix16(0);
        }
        Fix16((((self.0 as i64) << 16) / rhs.0 as i64) as i32)
    }
}

impl Neg for Fix16 {
    type Output = Self;
    #[inline]
    fn neg(self) -> Self {
        Fix16(-self.0)
    }
}

/* Debug表示: Fix16(65536) -> "1.0000" */
impl fmt::Debug for Fix16 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let int_part = self.0 >> 16;
        let frac_part = (self.0 & 0xFFFF) as u32;
        /* 小数部を4桁に変換: frac * 10000 / 65536 */
        let frac_decimal = (frac_part * 10000) / 65536;
        if self.0 < 0 && int_part == 0 {
            write!(f, "-0.{:04}", frac_decimal)
        } else {
            write!(f, "{}.{:04}", int_part, frac_decimal)
        }
    }
}

/* ====================================================================== */
/*  C互換FFI関数 (extern "C" 公開)                                         */
/*                                                                          */
/*  C版プログラムからリンク可能な関数名を提供する。                          */
/*  将来的にCライブラリを完全に置き換える際に使用。                          */
/* ====================================================================== */

#[no_mangle]
pub extern "C" fn fix16_mul(a: i32, b: i32) -> i32 {
    (Fix16(a) * Fix16(b)).0
}

#[no_mangle]
pub extern "C" fn fix16_div(a: i32, b: i32) -> i32 {
    (Fix16(a) / Fix16(b)).0
}

#[no_mangle]
pub extern "C" fn fix16_from_frac(num: i32, den: i32) -> i32 {
    Fix16::from_frac(num, den).0
}

#[no_mangle]
pub extern "C" fn fix16_abs(x: i32) -> i32 {
    Fix16(x).abs().0
}

#[no_mangle]
pub extern "C" fn fix16_ceil(x: i32) -> i32 {
    Fix16(x).ceil().0
}

#[no_mangle]
pub extern "C" fn fix16_floor(x: i32) -> i32 {
    Fix16(x).floor().0
}

#[no_mangle]
pub extern "C" fn fix16_round(x: i32) -> i32 {
    Fix16(x).round().0
}

#[no_mangle]
pub extern "C" fn fix16_min(a: i32, b: i32) -> i32 {
    Fix16(a).min(Fix16(b)).0
}

#[no_mangle]
pub extern "C" fn fix16_max(a: i32, b: i32) -> i32 {
    Fix16(a).max(Fix16(b)).0
}

#[no_mangle]
pub extern "C" fn fix16_clamp(x: i32, lo: i32, hi: i32) -> i32 {
    Fix16(x).clamp(Fix16(lo), Fix16(hi)).0
}

/* ====================================================================== */
/*  テスト                                                                  */
/* ====================================================================== */

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_from_int() {
        assert_eq!(Fix16::from_int(1).raw(), 65536);
        assert_eq!(Fix16::from_int(-1).raw(), -65536);
        assert_eq!(Fix16::from_int(0).raw(), 0);
    }

    #[test]
    fn test_to_int() {
        assert_eq!(Fix16::from_int(42).to_int(), 42);
        assert_eq!(Fix16(65536 + 32768).to_int(), 1); /* 1.5 -> 1 */
    }

    #[test]
    fn test_mul() {
        let a = Fix16::from_int(3);
        let b = Fix16::from_int(4);
        assert_eq!((a * b).to_int(), 12);

        /* 0.5 * 0.5 = 0.25 */
        let half = Fix16::HALF;
        let quarter = half * half;
        assert_eq!(quarter.raw(), 16384); /* 0.25 * 65536 = 16384 */
    }

    #[test]
    fn test_div() {
        let a = Fix16::from_int(10);
        let b = Fix16::from_int(3);
        let result = a / b;
        /* 10/3 = 3.3333... -> raw ~ 218453 */
        assert_eq!(result.to_int(), 3);
        assert!((result.raw() - 218453).abs() <= 1);
    }

    #[test]
    fn test_div_zero() {
        let a = Fix16::from_int(10);
        let b = Fix16::ZERO;
        assert_eq!((a / b).raw(), 0);
    }

    #[test]
    fn test_ceil_floor_round() {
        let v = Fix16(65536 + 32768); /* 1.5 */
        assert_eq!(v.ceil().to_int(), 2);
        assert_eq!(v.floor().to_int(), 1);
        assert_eq!(v.round().to_int(), 2);

        let v2 = Fix16(65536 + 16384); /* 1.25 */
        assert_eq!(v2.round().to_int(), 1);
    }

    #[test]
    fn test_clamp() {
        let lo = Fix16::from_int(0);
        let hi = Fix16::from_int(100);
        assert_eq!(Fix16::from_int(-5).clamp(lo, hi), lo);
        assert_eq!(Fix16::from_int(50).clamp(lo, hi), Fix16::from_int(50));
        assert_eq!(Fix16::from_int(200).clamp(lo, hi), hi);
    }

    #[test]
    fn test_from_frac() {
        let half = Fix16::from_frac(1, 2);
        assert_eq!(half.raw(), 32768);

        let third = Fix16::from_frac(1, 3);
        assert_eq!(third.to_int(), 0);
        assert!((third.raw() - 21845).abs() <= 1);
    }
}
