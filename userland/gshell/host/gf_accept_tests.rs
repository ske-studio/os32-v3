use super::*;
use crate::{input::Ctx, mocks, typical_ops, wm};

unsafe extern "C" fn clock(lo: *mut u32, hi: *mut u32) -> i32 {
    *lo = 100; *hi = 0; 0
}
fn setup() -> wm::GuiState {
    mocks::init();
    unsafe { (*os32api::api_ptr()).sys_time_now = clock; }
    let s = state();
    s.active = false; s.paused = false; s.warm = 0; s.count = 0;
    let mut st = wm::GuiState::NEW;
    st.screen_w = 640; st.screen_h = 480;
    st
}
fn present() {
    let p = present_begin(); present_end(p);
}

#[test]
fn p0a_idle_cycles_preserve_gfw1_count() {
    let mut st = setup();
    // Settle initial cursor/taskbar damage before measuring idle cycles.
    pause(true);
    wm::wm_cycle(&mut st, Ctx::Standalone);
    pause(false);
    for _ in 0..10 { present(); }
    assert_eq!(state().warm, 10);
    for expected in 0..2 {
        assert_eq!(state().count, expected);
        for _ in 0..3 {
            for ctx in [Ctx::Standalone, Ctx::Wait, Ctx::Pump] {
                wm::wm_cycle(&mut st, ctx);
                assert_eq!(state().count, expected, "idle cycle added a GFW1 sample");
            }
        }
        present();
    }
    assert_eq!(state().count, 2);
    state().count = 0;
}

#[test]
fn p0a_busy_between_gfw1_cycles() {
    let mut st = setup();
    assert!(!busy());
    for _ in 0..10 { present(); }
    assert!(!busy());
    present();
    assert!(!state().active);
    assert!(busy());
    assert_eq!(typical_ops::start(&mut st, b"12345678", 3, 1), Err(4));
    for _ in 1..COUNT { present(); }
    assert_eq!(state().count, COUNT);
    assert!(!busy());
    assert_eq!(typical_ops::start(&mut st, b"12345678", 3, 1), Ok(()));
    pause(false);
    state().count = 0;
}
