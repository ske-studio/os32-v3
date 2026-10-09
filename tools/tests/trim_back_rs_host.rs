// Host seams only: production run_vt and trim trampoline are appended verbatim.
#![no_std]
#![allow(dead_code, static_mut_refs)]
extern crate alloc;
use core::ffi::c_void;
use os32api::gui::{GuiEvent, GUI_EV_TRIM, GUI_OP_TRIM_DONE, GUI_RING_CAPACITY};
use os32api::api;
#[path = "CACHE_SOURCE"] mod cache;
use cache::{Cache, Stat};
extern "C" {
    fn g4_check(ok: i32, label: *const u8);
    fn g4_case() -> u32;
    fn g4_epoch() -> u32;
    fn g4_stop();
    fn g4_pass();
    fn g4_raw_map_test();
    fn g4_unarmed_done();
    fn g4_consume_returned();
}
fn check(ok: bool, label: &'static [u8]) { unsafe { g4_check(ok as i32, label.as_ptr()); } }
static mut CACHE: Cache = Cache::new();
static mut HANDLER: bool = false;
static mut COMMITS: u32 = 0;
static mut AFTER: u32 = 0;
static mut DONE: u32 = 0;
static mut DONE_PAGES: u32 = 0;
static mut HOOK: u32 = 0;
type GuiResult<T> = Result<T, ()>;
struct AppVTable;
struct Ui { timeout_ticks: u32 }
struct VApp { vt: *const AppVTable, this: *mut c_void }
impl VApp {
    fn on_overflow(&self, _: &mut Ui, _: u32) {}
    fn after_commit(&self, _: &mut Ui) { unsafe { AFTER += 1; } }
}
struct State {
    trim_batch: bool, trim_epoch: u32, hook: Option<extern "C" fn() -> u32>,
    trim_events: u32, trim_done_sent: u32, hook_calls: u32, hook_pages: u32,
    input_unknown: bool, quit: bool,
}
static mut STATE: State = State { trim_batch:false, trim_epoch:0, hook:None,
    trim_events:0, trim_done_sent:0, hook_calls:0, hook_pages:0, input_unknown:false, quit:false };
fn s() -> &'static mut State { unsafe { &mut STATE } }
mod client {
    use super::*;
    pub struct GuiErr;
    impl GuiErr { pub const INVAL: () = (); }
    pub struct Poll { pub count: usize, pub overflow: bool, pub dropped: u32 }
    pub fn enter_handler() { unsafe { HANDLER = true; } }
    pub fn leave_handler() { unsafe { HANDLER = false; } }
    pub fn dbg_print_num(_: &[u8], _: i32) {}
    pub fn poll(buf: &mut [GuiEvent]) -> GuiResult<Poll> {
        let epoch = unsafe { g4_epoch() };
        let state = wm::GuiState;
        trim::deliver(&state);
        check(unsafe { trim::gshell_trim_delivered == 1 }, b"one WM delivery\0");
        buf[0] = unsafe { ring::EVENT };
        check(buf[0].payload[..4] == epoch.to_le_bytes(), b"TRIM epoch\0");
        Ok(Poll { count:1, overflow:false, dropped:0 })
    }
    pub fn commit(_: u32) -> GuiResult<()> { unsafe { COMMITS += 1; } Ok(()) }
    pub fn call(op: u32, epoch: u32) -> GuiResult<i32> {
        check(unsafe { HOOK == 1 && !HANDLER && COMMITS == 1 && AFTER == 1 }, b"hook before DONE at safe point\0");
        check(op == GUI_OP_TRIM_DONE, b"DONE op\0");
        let rc = unsafe { (api().gui_call)(op, epoch) };
        check(rc > 0, b"DONE returns kernel pages\0");
        unsafe { DONE += 1; DONE_PAGES = rc as u32; }
        s().quit = true;
        Ok(rc)
    }
    pub fn wait(_: u32) -> GuiResult<()> { check(false, b"unexpected WAIT\0"); Ok(()) }
}
mod window { pub fn count() -> usize { 1 } }
fn drain_pending_focus(_: &VApp, _: &mut Ui) {}
fn flush_damage() {}
fn paint_damaged(_: &VApp, _: &mut Ui) {}
fn dispatch(_: &VApp, _: &mut Ui, _: &GuiEvent) { check(false, b"TRIM hidden from app\0"); }
fn hook() -> u32 {
    unsafe {
        HOOK += 1;
        check(!HANDLER && AFTER == 1 && COMMITS == 1, b"hook safe point\0");
        if g4_case() == 2 { g4_stop(); }
        let pages = CACHE.release();
        check(CACHE.data_crc() == CACHE.crc && !CACHE.bad, b"DATA CRC retained\0");
        pages
    }
}
mod multiapp {
    pub const MAX_APPS: usize = 4;
    pub const APP_ID_MIN: i32 = 2;
    pub const APP_ID_MAX: i32 = 5;
    pub const APP_STATE_PARKED: i32 = 2;
    pub fn is_tracked(id: i32) -> bool { id == 2 }
}
mod wm {
    pub struct GuiState;
    impl GuiState {
        pub fn front_owner(&self) -> i32 { 3 }
        pub fn slot_of_owner(&self, id: i32) -> Option<usize> { if id == 2 { Some(0) } else { None } }
    }
}
mod ring {
    use super::*;
    pub static mut EVENT: GuiEvent = GuiEvent { kind:0, sub:0, serial:0, window:0, payload:[0;8] };
    pub fn ev_trim(epoch: u32) -> GuiEvent {
        let mut ev = GuiEvent { kind:GUI_EV_TRIM, sub:0, serial:0, window:0, payload:[0;8] };
        ev.payload[..4].copy_from_slice(&epoch.to_le_bytes()); ev
    }
    pub fn append(_: &wm::GuiState, _: usize, ev: &GuiEvent) -> bool { unsafe { EVENT = *ev; } true }
}
mod trim { include!("wm_trim.rs"); pub fn was_sent() -> bool { unsafe { (*SENT.0.get())[0] } } }
#[no_mangle]
pub extern "C" fn g4_forget(id: i32) {
    check(trim::was_sent(), b"WM sent before forget\0");
    trim::forget(id);
    check(!trim::was_sent(), b"WM sent cleared\0");
}
#[no_mangle]
pub extern "C" fn g4_rust_run() {
    let mut table = host_api();
    os32api::os32_init(&mut table);
    os32api::retry_enable();
    unsafe {
        check(!CACHE.passed(), b"unfinished exit fails\0");
        check(CACHE.prepare().is_some(), b"Rust PREP ARENA=2 LARGE=0\0");
        check(CACHE.before.large() == 0 && CACHE.before.arenas() == 2, b"Rust LARGE zero\0");
        check(CACHE.release() == 0 && !CACHE.released, b"unarmed keeps cache\0");
        if g4_case() == 3 { g4_unarmed_done(); }
        g4_raw_map_test();
        CACHE.armed = true;
        set_trim_hook(hook);
        s().hook = Some(trim_trampoline);
        let mut ui = Ui { timeout_ticks:0 };
        check(run_vt(&AppVTable, core::ptr::null_mut(), &mut ui).is_ok(), b"run_vt succeeds\0");
        check(HOOK == 1 && DONE == 1 && s().trim_events == 1 && s().trim_done_sent == 1, b"one TRIM hook DONE\0");
        if g4_case() == 3 { g4_consume_returned(); }
        check(CACHE.observe_done(), b"G4 tail whole INITIAL DATA pages\0");
        check(CACHE.pages == DONE_PAGES, b"extent page count equals DONE result\0");
        check(!CACHE.passed(), b"missing regrow exit fails\0");
        if g4_case() == 1 { g4_stop(); }
        check(CACHE.regrow(), b"Rust EXACT regrow\0");
        check(CACHE.passed(), b"complete exit succeeds\0");
        check(os32api::retry_stats() == (0, 0, 0), b"allocator retry unused\0");
        g4_pass();
    }
}
