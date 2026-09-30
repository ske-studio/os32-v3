/*
 * math_test_rs — os32_math crateのゲスト側テストプログラム
 *
 * C版 math_test と同等のテストをRust版ライブラリで実行し、
 * 結果をコンソールに出力する。
 */
#![no_std]
#![no_main]

extern crate os32api;
extern crate os32_math;

use os32api::KernelAPI;
use os32api::{kprint, kprint_attr};
use os32_math::fix16::Fix16;
use os32_math::vec2::Vec2;
use os32_math::trig;
use os32_math::sqrt;
use os32_math::atan2;
use os32_math::random::Rng;
use os32_math::lerp;
use os32_math::recip;

/* テスト結果カウンタ */
static mut PASS: i32 = 0;
static mut FAIL: i32 = 0;

unsafe fn check(name: &[u8], ok: bool) {
    if ok {
        PASS += 1;
        kprint_attr!(os32api::ATTR_GREEN, b"  [OK] %s\r\n\0", name.as_ptr());
    } else {
        FAIL += 1;
        kprint_attr!(os32api::ATTR_RED, b"  [NG] %s\r\n\0", name.as_ptr());
    }
}

#[no_mangle]
pub extern "C" fn main(
    _argc: i32,
    _argv: *const *const u8,
    api: *mut KernelAPI,
) -> i32 {
    os32api::os32_init(api);

    kprint!(b"=== os32_math Rust crate test ===\r\n\0");
    kprint!(b"\r\n\0");

    unsafe {
        /* --- Fix16 基本演算 --- */
        kprint_attr!(os32api::ATTR_YELLOW, b"[Fix16]\r\n\0");

        let a = Fix16::from_int(3);
        let b = Fix16::from_int(4);
        check(b"3 * 4 = 12\0", (a * b).to_int() == 12);

        let c = Fix16::from_int(10);
        let d = Fix16::from_int(3);
        check(b"10 / 3 = 3\0", (c / d).to_int() == 3);

        let half = Fix16::HALF;
        check(b"0.5 * 0.5 = 0.25\0", (half * half).raw() == 16384);

        let v = Fix16::from_raw(65536 + 32768); /* 1.5 */
        check(b"ceil(1.5) = 2\0", v.ceil().to_int() == 2);
        check(b"floor(1.5) = 1\0", v.floor().to_int() == 1);
        check(b"round(1.5) = 2\0", v.round().to_int() == 2);

        check(b"clamp(-5, 0, 100) = 0\0",
            Fix16::from_int(-5).clamp(Fix16::from_int(0), Fix16::from_int(100))
            == Fix16::from_int(0));

        /* --- Trig --- */
        kprint_attr!(os32api::ATTR_YELLOW, b"[Trig]\r\n\0");

        check(b"sin(0) = 0\0", trig::isin(0) == 0);
        check(b"sin(128) = 32767\0", trig::isin(128) == 32767);
        check(b"cos(0) = 32767\0", trig::icos(0) == 32767);
        check(b"deg_to_idx(90) = 128\0", trig::deg_to_idx(90) == 128);

        /* --- Sqrt --- */
        kprint_attr!(os32api::ATTR_YELLOW, b"[Sqrt]\r\n\0");

        check(b"isqrt(100) = 10\0", sqrt::isqrt(100) == 10);
        check(b"isqrt(10000) = 100\0", sqrt::isqrt(10000) == 100);
        check(b"fast_dist_sq(3,4) = 25\0", sqrt::fast_distance_sq(3, 4) == 25);

        let fd = sqrt::fast_distance(3, 4);
        check(b"fast_dist(3,4) ~ 5\0", fd >= 4 && fd <= 6);

        /* --- Atan2 --- */
        kprint_attr!(os32api::ATTR_YELLOW, b"[Atan2]\r\n\0");

        check(b"atan2(0,100) = 0\0", atan2::iatan2(0, 100) == 0);
        let a90 = atan2::iatan2(100, 0);
        check(b"atan2(100,0) ~ 128\0", (a90 - 128).abs() <= 2);

        /* --- Random --- */
        kprint_attr!(os32api::ATTR_YELLOW, b"[Random]\r\n\0");

        let mut rng = Rng::new();
        let v1 = rng.next();
        let v2 = rng.next();
        check(b"rng produces values\0", v1 != 0 && v2 != 0 && v1 != v2);

        let mut ok = true;
        let mut i = 0;
        while i < 100 {
            let r = rng.range(0, 10);
            if r < 0 || r > 10 { ok = false; }
            i += 1;
        }
        check(b"rng_range(0,10) in bounds\0", ok);

        /* --- Vec2 --- */
        kprint_attr!(os32api::ATTR_YELLOW, b"[Vec2]\r\n\0");

        let va = Vec2::new(Fix16::from_int(3), Fix16::from_int(4));
        let vb = Vec2::new(Fix16::from_int(1), Fix16::from_int(2));
        let vc = va + vb;
        check(b"(3,4)+(1,2) = (4,6)\0",
            vc.x.to_int() == 4 && vc.y.to_int() == 6);

        let len = va.length();
        check(b"|(3,4)| ~ 5\0", (len.to_int() - 5).abs() <= 1);

        check(b"Vec2::ZERO normalize = ZERO\0",
            Vec2::ZERO.normalize() == Vec2::ZERO);

        /* --- Lerp --- */
        kprint_attr!(os32api::ATTR_YELLOW, b"[Lerp]\r\n\0");

        let la = Fix16::from_int(0);
        let lb = Fix16::from_int(100);
        let lresult = lerp::fix16_lerp(la, lb, Fix16::HALF);
        check(b"lerp(0,100,0.5) = 50\0", lresult.to_int() == 50);

        check(b"lerp_int(0,100,5,10) = 50\0",
            lerp::lerp_int(0, 100, 5, 10) == 50);

        check(b"ease_in_quad(0) = 0\0",
            lerp::ease_in_quad(Fix16::ZERO) == Fix16::ZERO);

        /* --- Recip --- */
        kprint_attr!(os32api::ATTR_YELLOW, b"[Recip]\r\n\0");

        let ra = Fix16::from_int(10);
        check(b"fast_div(10,2) = 5\0",
            recip::fast_div(ra, 2).to_int() == 5);

        /* --- 結果サマリ --- */
        kprint!(b"\r\n\0");
        kprint!(b"Results: %d passed, %d failed\r\n\0", PASS, FAIL);

        if FAIL == 0 {
            kprint_attr!(os32api::ATTR_GREEN, b"All tests passed!\r\n\0");
        } else {
            kprint_attr!(os32api::ATTR_RED, b"Some tests FAILED!\r\n\0");
        }
    }

    0
}
