/* lerp.rs — 線形補間・イージング関数群
 *
 * C版 lerp.c の Rust 移植。
 * UIアニメーション、ゲームオブジェクトの滑らかな移動に使用。
 * すべての関数は Q16.16 固定小数点で動作する。
 */

use crate::fix16::Fix16;

/* 線形補間: a + (b - a) * t */
pub fn fix16_lerp(a: Fix16, b: Fix16, t: Fix16) -> Fix16 {
    a + (b - a) * t
}

/* 整数線形補間: a + (b - a) * t / t_max */
pub fn lerp_int(a: i32, b: i32, t: i32, t_max: i32) -> i32 {
    if t_max <= 0 { return a; }
    if t <= 0 { return a; }
    if t >= t_max { return b; }
    a + (((b - a) as i64 * t as i64) / t_max as i64) as i32
}

/* ease_in_quad: f(t) = t^2 */
pub fn ease_in_quad(t: Fix16) -> Fix16 {
    t * t
}

/* ease_out_quad: f(t) = 1 - (1-t)^2 */
pub fn ease_out_quad(t: Fix16) -> Fix16 {
    let inv = Fix16::ONE - t;
    Fix16::ONE - inv * inv
}

/* ease_in_out_quad: S字カーブ */
pub fn ease_in_out_quad(t: Fix16) -> Fix16 {
    if t < Fix16::HALF {
        Fix16::from_raw((t * t).raw() << 1)
    } else {
        let inv = Fix16::ONE - t;
        Fix16::ONE - Fix16::from_raw((inv * inv).raw() << 1)
    }
}

/* ease_in_cubic: f(t) = t^3 */
pub fn ease_in_cubic(t: Fix16) -> Fix16 {
    t * t * t
}

/* ease_out_cubic: f(t) = 1 - (1-t)^3 */
pub fn ease_out_cubic(t: Fix16) -> Fix16 {
    let inv = Fix16::ONE - t;
    Fix16::ONE - inv * inv * inv
}

/* ease_in_out_cubic: 3次S字カーブ */
pub fn ease_in_out_cubic(t: Fix16) -> Fix16 {
    if t < Fix16::HALF {
        let t2 = t * t;
        Fix16::from_raw((t2 * t).raw() << 2)
    } else {
        let inv = Fix16::ONE - t;
        let inv2 = inv * inv;
        Fix16::ONE - Fix16::from_raw((inv2 * inv).raw() << 2)
    }
}

/* ease_bounce: バウンスイージング */
pub fn ease_bounce(t: Fix16) -> Fix16 {
    let n1 = Fix16::from_raw(495616); /* 7.5625 * 65536 */

    if t.raw() < 23831 {
        n1 * t * t
    } else if t.raw() < 47663 {
        let d = Fix16::from_raw(t.raw() - 35747);
        n1 * d * d + Fix16::from_raw(49152)  /* + 0.75 */
    } else if t.raw() < 59578 {
        let d = Fix16::from_raw(t.raw() - 53620);
        n1 * d * d + Fix16::from_raw(61440)  /* + 15/16 */
    } else {
        let d = Fix16::from_raw(t.raw() - 62532);
        n1 * d * d + Fix16::from_raw(64512)  /* + 63/64 */
    }
}

/* C互換FFI関数 */
#[no_mangle]
pub extern "C" fn rs_fix16_lerp(a: i32, b: i32, t: i32) -> i32 {
    fix16_lerp(Fix16::from_raw(a), Fix16::from_raw(b), Fix16::from_raw(t)).raw()
}

#[no_mangle]
pub extern "C" fn rs_lerp_int(a: i32, b: i32, t: i32, t_max: i32) -> i32 {
    lerp_int(a, b, t, t_max)
}

#[no_mangle]
pub extern "C" fn rs_ease_in_quad(t: i32) -> i32 { ease_in_quad(Fix16::from_raw(t)).raw() }

#[no_mangle]
pub extern "C" fn rs_ease_out_quad(t: i32) -> i32 { ease_out_quad(Fix16::from_raw(t)).raw() }

#[no_mangle]
pub extern "C" fn rs_ease_bounce(t: i32) -> i32 { ease_bounce(Fix16::from_raw(t)).raw() }

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lerp() {
        let a = Fix16::from_int(0);
        let b = Fix16::from_int(100);
        let half = Fix16::HALF;
        let result = fix16_lerp(a, b, half);
        assert_eq!(result.to_int(), 50);
    }

    #[test]
    fn test_lerp_int() {
        assert_eq!(lerp_int(0, 100, 5, 10), 50);
        assert_eq!(lerp_int(0, 100, 0, 10), 0);
        assert_eq!(lerp_int(0, 100, 10, 10), 100);
    }

    #[test]
    fn test_ease_boundaries() {
        /* t=0 のとき全イージング関数は 0 を返すべき */
        let zero = Fix16::ZERO;
        assert_eq!(ease_in_quad(zero), Fix16::ZERO);
        assert_eq!(ease_out_quad(zero), Fix16::ZERO);
        assert_eq!(ease_in_cubic(zero), Fix16::ZERO);

        /* t=1 のとき全イージング関数は ~1.0 を返すべき */
        let one = Fix16::ONE;
        assert_eq!(ease_in_quad(one).to_int(), 1);
        assert_eq!(ease_out_quad(one).to_int(), 1);
        assert_eq!(ease_in_cubic(one).to_int(), 1);
    }
}
