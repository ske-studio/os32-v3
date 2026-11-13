/// WM polling timestamp in milliseconds (low 32 bits).
/// 待つのは sys_halt。ここで回して待たない ([V3] の get_tick スピン禁止)。
#[inline]
pub fn poll_now() -> u32 {
    let mut lo = 0;
    let mut hi = 0;
    unsafe {
        let api = os32api::api();
        if (api.sys_time_now)(&mut lo, &mut hi) == 0 {
            ((((hi as u64) << 32) | lo as u64) / 1000) as u32
        } else {
            (api.get_tick)().wrapping_mul(10)
        }
    }
}
