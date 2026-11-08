use super::*;
use crate::{mocks, slot};
use std::sync::atomic::{AtomicU32, Ordering::SeqCst};
static US: AtomicU32 = AtomicU32::new(100);
static COMMITS: AtomicU32 = AtomicU32::new(0);
unsafe extern "C" fn clock(lo: *mut u32, hi: *mut u32) -> i32 {
    *lo = US.load(SeqCst); *hi = 0; 0
}
unsafe extern "C" fn counters(out: *mut u8) -> i32 {
    let n = COMMITS.load(SeqCst);
    *(out as *mut GfxStats) = GfxStats { present_bytes: n*32, commits: n, hw_ops: n*2, io_accesses: 0 }; 0
}
fn setup() -> GuiState {
    mocks::init(); wm::gf_accept::pause(false);
    unsafe {
        let a = &mut *os32api::api_ptr();
        a.sys_time_now = clock; a.gfx_stats = counters;
    }
    US.store(100, SeqCst); COMMITS.store(0, SeqCst); mocks::set_tick(0);
    GuiState::NEW
}
fn output() -> String {
    String::from_utf8(std::mem::take(&mut *mocks::SERIAL_OUT.lock().unwrap())).unwrap()
}
fn pending_run(st: &mut GuiState) {
    start(st, b"12345678", 3, 1).unwrap();
    // Clock/counters are the real KAPI boundary; avoid drawing in this fixture.
    st.ops.waiting = true; st.ops.t0 = 100; st.ops.deadline = 200;
    st.ops.warm = WARM;
}
#[test]
fn p0a_parse() {
    for (i, name) in NAMES.iter().enumerate() {
        assert_eq!(gserial::parse(format!("ops {name}").as_bytes()), gserial::Cmd::Ops(i as u8, 50));
        assert_eq!(parse(format!("{name} 300").as_bytes()), Some((i as u8, 300)));
    }
    for s in ["", "bogus", "char 0", "move 301", "move 65536", "move -1", "move 1 x", "move "] {
        assert_eq!(parse(s.as_bytes()), None, "{s}");
    }
}
#[test]
fn p0a_preconditions() {
    let mut st = setup();
    assert_eq!(start(&mut st, b"12345678", 0, 1), Err(3));
    start(&mut st, b"12345678", 4, 1).unwrap();
    assert!(output().contains("GOP1 char SKIP title"));
    st.windows[0].used = true; st.windows[0].visible = true; st.zorder[0] = 0; st.z_count = 1;
    st.windows[0].title[..12].copy_from_slice(b"File Manager");
    assert_eq!(start(&mut st, b"12345678", 4, 1), Err(3));
    start(&mut st, b"12345678", 5, 1).unwrap();
    assert_eq!(start(&mut st, b"12345678", 3, 1), Err(4));
    wm::gf_accept::pause(false);
}
#[test]
fn p0a_completion_four_conditions() {
    let mut st = setup();
    let shm = mocks::Shm::new(); st.shm_base = shm.base();
    st.slots[0].used = true; st.slots[0].owner = 2; slot::init_header(&st, 0);
    st.windows[0].used = true;
    assert!(drained(&st, true));
    st.windows[0].dirty.len = 1; assert!(!drained(&st, true)); st.windows[0].dirty.clear();
    st.windows[0].issued.len = 1; assert!(!drained(&st, true)); st.windows[0].issued.clear();
    st.windows[0].configure_pending = true; assert!(!drained(&st, true)); st.windows[0].configure_pending = false;
    let mut h = slot::read_header(&st, 0); h.ring_tail = 1; slot::write_header(&st, 0, &h);
    assert!(!drained(&st, true)); h.ring_head = 1; slot::write_header(&st, 0, &h);
    st.pending_raw_n = 1; assert!(!drained(&st, true)); st.pending_raw_n = 0;
    gserial::enqueue(&mut st, b"37", false).unwrap(); assert!(!drained(&st, true));
    gserial::deliver(&mut st); st.pending_raw_n = 0;
    assert!(!drained(&st, false)); assert!(drained(&st, true));
}
#[test]
fn p0a_timeout_wrap_and_failed() {
    let mut st = setup(); pending_run(&mut st);
    st.ops.deadline = 5; mocks::set_tick(u32::MAX); on_cycle_end(&mut st);
    assert!(st.ops.active); assert!(output().is_empty());
    mocks::set_tick(5); on_cycle_end(&mut st);
    assert!(!st.ops.active);
    let out = output();
    assert!(out.contains("GOP1 desktop 0 FAILED 0 0 0\r\n"), "{out}");
    assert!(out.contains("p50 FAILED p95 FAILED valid 0 failed 1"));
    assert!(out.ends_with("gs:12345678 end 0\r\n"));
}
#[test]
fn p0a_warmup_samples_and_percentiles() {
    let mut st = setup(); pending_run(&mut st); st.ops.warm = 0; st.ops.left = 50;
    for i in 0..60 {
        st.ops.waiting = true; st.ops.t0 = 100;
        st.ops.base = stats(); st.ops.last_commit = st.ops.base.commits;
        COMMITS.fetch_add(1, SeqCst); US.store(101+i, SeqCst);
        on_cycle_end(&mut st);
        if i < 10 { assert_eq!(st.ops.i, 0); }
    }
    let out = output();
    assert_eq!(out.lines().filter(|l| l.starts_with("GOP1 desktop ") && !l.contains("p50")).count(), 50);
    assert!(out.contains("desktop 0 11 32 1 2"));
    assert!(out.contains("p50 35 p95 58 valid 50 failed 0"), "{out}");
    assert!(!st.ops.active);
}
#[test]
fn p0a_key_injection_and_roundtrip() {
    let mut st = setup();
    for (op, key) in [(1, b"37".as_slice()), (2, b"00+c"), (4, b"1d"), (5, b"3d")] {
        st.ops.op = op;
        assert!(inject(&mut st)); assert!(gserial::queued(&st));
        // Compare the translated raw input with Cmd::Key's queue path.
        gserial::deliver(&mut st); let raw = st.pending_raw; let n = st.pending_raw_n;
        st.pending_raw_n = 0;
        gserial::enqueue(&mut st, key, false).unwrap(); gserial::deliver(&mut st);
        assert_eq!(&raw[..n], &st.pending_raw[..st.pending_raw_n]); st.pending_raw_n = 0;
    }
    st.ops = Run::NEW; pending_run(&mut st); st.ops.op = 2;
    COMMITS.store(1, SeqCst); on_cycle_end(&mut st);
    assert!(st.ops.active); assert_eq!(st.ops.phase, 1); assert_eq!(st.ops.i, 0);
    wm::gf_accept::pause(false);
}
#[test]
fn p0a_wm_cycle_starts_script_after_flush() {
    let mut st = setup();
    st.screen_w = 640; st.screen_h = 480;
    start(&mut st, b"12345678", 3, 1).unwrap();
    wm::wm_cycle(&mut st, crate::input::Ctx::Standalone);
    assert!(st.ops.waiting);
    assert_eq!(st.ops.t0, 100);
    assert_eq!(st.ops.deadline, 200);
    wm::gf_accept::pause(false);
}
#[test]
fn p0a_move_goes_right_and_returns() {
    let mut st = setup();
    st.screen_w = 640; st.screen_h = 480;
    let w = &mut st.windows[0];
    w.used = true; w.visible = true; w.owner = 2; w.gen = 1;
    w.x = 30; w.y = 40; w.w = 320; w.h = 240;
    st.zorder[0] = 0; st.z_count = 1;
    start(&mut st, b"12345678", 0, 1).unwrap();
    on_cycle_end(&mut st);
    assert_eq!(st.windows[0].x, 94);
    st.windows[0].dirty.clear(); st.windows[0].configure_pending = false;
    COMMITS.store(1, SeqCst); US.store(110, SeqCst);
    on_cycle_end(&mut st);
    assert_eq!(st.windows[0].x, 30); assert_eq!(st.ops.warm, 0);
    st.windows[0].dirty.clear(); st.windows[0].configure_pending = false;
    COMMITS.store(2, SeqCst); US.store(120, SeqCst);
    on_cycle_end(&mut st);
    assert_eq!(st.ops.warm, 1); assert!(!st.ops.waiting);
    wm::gf_accept::pause(false);
}
