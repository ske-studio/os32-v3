/* vec2.rs — 2Dベクトル演算
 *
 * C版 vec2.c の Rust 移植。
 * fix16_t ベースの2Dベクトル。ゲームのキャラクター移動、弾幕計算、
 * 物理演算などに使用する。
 */

use core::ops::{Add, Sub, Mul, Neg};
use crate::fix16::Fix16;
use crate::trig::{isin, icos, ISIN_SCALE};
use crate::sqrt::fix16_sqrt;

#[derive(Copy, Clone, PartialEq, Eq, Default, Debug)]
pub struct Vec2 {
    pub x: Fix16,
    pub y: Fix16,
}

impl Vec2 {
    pub const ZERO: Vec2 = Vec2 { x: Fix16::ZERO, y: Fix16::ZERO };

    #[inline]
    pub const fn new(x: Fix16, y: Fix16) -> Self {
        Vec2 { x, y }
    }

    /* 内積 */
    pub fn dot(self, other: Self) -> Fix16 {
        self.x * other.x + self.y * other.y
    }

    /* 2D外積 (スカラー値) */
    pub fn cross(self, other: Self) -> Fix16 {
        self.x * other.y - self.y * other.x
    }

    /* 長さの二乗 */
    pub fn length_sq(self) -> Fix16 {
        self.x * self.x + self.y * self.y
    }

    /* 長さ */
    pub fn length(self) -> Fix16 {
        fix16_sqrt(self.length_sq())
    }

    /* 正規化 (長さ1.0) */
    pub fn normalize(self) -> Self {
        let len = self.length();
        if len == Fix16::ZERO { return Self::ZERO; }
        Vec2 { x: self.x / len, y: self.y / len }
    }

    /* 回転 (512分割角度) */
    pub fn rotate(self, angle: i32) -> Self {
        let c = icos(angle);
        let s = isin(angle);
        Vec2 {
            x: Fix16::from_raw(
                ((self.x.raw() as i64 * c as i64
                - self.y.raw() as i64 * s as i64) / ISIN_SCALE as i64) as i32
            ),
            y: Fix16::from_raw(
                ((self.x.raw() as i64 * s as i64
                + self.y.raw() as i64 * c as i64) / ISIN_SCALE as i64) as i32
            ),
        }
    }

    /* 2点間距離 */
    pub fn distance(self, other: Self) -> Fix16 {
        (other - self).length()
    }

    /* 線形補間 (t=0.0 で self, t=1.0 で other) */
    pub fn lerp(self, other: Self, t: Fix16) -> Self {
        Vec2 {
            x: self.x + (other.x - self.x) * t,
            y: self.y + (other.y - self.y) * t,
        }
    }

    /* スカラー倍 */
    pub fn scale(self, s: Fix16) -> Self {
        Vec2 { x: self.x * s, y: self.y * s }
    }
}

impl Add for Vec2 {
    type Output = Self;
    #[inline]
    fn add(self, rhs: Self) -> Self {
        Vec2 { x: self.x + rhs.x, y: self.y + rhs.y }
    }
}

impl Sub for Vec2 {
    type Output = Self;
    #[inline]
    fn sub(self, rhs: Self) -> Self {
        Vec2 { x: self.x - rhs.x, y: self.y - rhs.y }
    }
}

impl Neg for Vec2 {
    type Output = Self;
    #[inline]
    fn neg(self) -> Self {
        Vec2 { x: -self.x, y: -self.y }
    }
}

/* Fix16スカラー倍: Vec2 * Fix16 */
impl Mul<Fix16> for Vec2 {
    type Output = Self;
    #[inline]
    fn mul(self, rhs: Fix16) -> Self {
        self.scale(rhs)
    }
}

/* C互換FFI関数 */
#[no_mangle]
pub extern "C" fn rs_vec2_dot(ax: i32, ay: i32, bx: i32, by: i32) -> i32 {
    let a = Vec2::new(Fix16::from_raw(ax), Fix16::from_raw(ay));
    let b = Vec2::new(Fix16::from_raw(bx), Fix16::from_raw(by));
    a.dot(b).raw()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_add_sub() {
        let a = Vec2::new(Fix16::from_int(1), Fix16::from_int(2));
        let b = Vec2::new(Fix16::from_int(3), Fix16::from_int(4));
        let c = a + b;
        assert_eq!(c.x.to_int(), 4);
        assert_eq!(c.y.to_int(), 6);
        let d = c - b;
        assert_eq!(d.x.to_int(), 1);
        assert_eq!(d.y.to_int(), 2);
    }

    #[test]
    fn test_dot() {
        let a = Vec2::new(Fix16::from_int(3), Fix16::from_int(4));
        let b = Vec2::new(Fix16::from_int(1), Fix16::from_int(0));
        assert_eq!(a.dot(b).to_int(), 3);
    }

    #[test]
    fn test_length() {
        let v = Vec2::new(Fix16::from_int(3), Fix16::from_int(4));
        let len = v.length();
        assert!((len.to_int() - 5).abs() <= 1);
    }

    #[test]
    fn test_normalize_zero() {
        let v = Vec2::ZERO;
        let n = v.normalize();
        assert_eq!(n, Vec2::ZERO);
    }
}
