//! TASK_FONTPERF F5: the fixed-point helpers of the integer raster (no float).
//! FreeType's FT_DivFix / FT_MulFix rounding (FreeType `ftcalc.c`, FTL; the
//! rules only, no code copied) and F2Dot14 component transforms.

/// FT_DivFix(a, b) for a >= 0, b > 0: (a << 16) / b, rounded half up.
pub const fn div_fix(a: i64, b: i64) -> i64 {
    ((a << 16) + b / 2) / b
}

/// FT_MulFix(value, scale) for scale >= 0: round half away from zero.
pub const fn mul_fix(value: i64, scale: i64) -> i64 {
    let magnitude = (value.abs() * scale + 0x8000) >> 16;
    if value < 0 {
        -magnitude
    } else {
        magnitude
    }
}

/// 2.14 fixed-point one (F2Dot14 1.0).
pub const F2DOT14_ONE: i32 = 1 << 14;

/// `a * x + c * y` with a, c in F2Dot14, rounded to nearest (half up).
pub const fn f2dot14_dot(a: i32, x: i32, c: i32, y: i32) -> i64 {
    (a as i64 * x as i64 + c as i64 * y as i64 + (1 << 13)) >> 14
}

/// n / d rounded to nearest (half away from zero), d > 0.
pub const fn div_round(n: i64, d: i64) -> i64 {
    if n < 0 {
        -((-n + d / 2) / d)
    } else {
        (n + d / 2) / d
    }
}

/// Smallest n >= 1 with n^4 * unit >= value (an integer fourth root, rounded up).
pub fn root4_ceil(value: u64, unit: u64, max: u32) -> Option<u32> {
    let mut n = 1u32;
    loop {
        let n2 = u64::from(n) * u64::from(n);
        if n2.checked_mul(n2)?.checked_mul(unit)? >= value {
            return Some(n);
        }
        if n >= max {
            return None;
        }
        n += 1;
    }
}
