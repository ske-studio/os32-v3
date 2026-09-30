/* random.rs — xorshift32 擬似乱数生成器
 *
 * C版 random.c の Rust 移植。
 * 周期 2^32-1。シフト+XOR のみで構成。
 */

use crate::fix16::Fix16;

pub struct Rng {
    state: u32,
}

impl Rng {
    pub const fn new() -> Self {
        Rng { state: 2463534242 }
    }

    pub fn seed(&mut self, s: u32) {
        self.state = if s != 0 { s } else { 2463534242 };
    }

    pub fn next(&mut self) -> u32 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.state = x;
        x
    }

    pub fn range(&mut self, min: i32, max: i32) -> i32 {
        if min >= max { return min; }
        let range = (max - min + 1) as u32;
        min + (self.next() % range) as i32
    }

    pub fn fix16(&mut self) -> Fix16 {
        Fix16::from_raw((self.next() & 0xFFFF) as i32)
    }
}

static mut GLOBAL_RNG: Rng = Rng::new();

#[no_mangle]
pub extern "C" fn rs_rng_seed(seed: u32) {
    unsafe { GLOBAL_RNG.seed(seed); }
}

#[no_mangle]
pub extern "C" fn rs_rng_next() -> u32 {
    unsafe { GLOBAL_RNG.next() }
}

#[no_mangle]
pub extern "C" fn rs_rng_range(min: i32, max: i32) -> i32 {
    unsafe { GLOBAL_RNG.range(min, max) }
}

#[no_mangle]
pub extern "C" fn rs_rng_fix16() -> i32 {
    unsafe { GLOBAL_RNG.fix16().raw() }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_deterministic() {
        let mut a = Rng::new();
        let mut b = Rng::new();
        for _ in 0..100 { assert_eq!(a.next(), b.next()); }
    }

    #[test]
    fn test_range() {
        let mut rng = Rng::new();
        for _ in 0..1000 {
            let v = rng.range(0, 10);
            assert!(v >= 0 && v <= 10);
        }
    }
}
