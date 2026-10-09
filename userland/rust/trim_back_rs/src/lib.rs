//! GK-4: a=arm, o=observe DONE, r=regrow, q=quit (incomplete => exit 1).
#![no_std]
#![no_main]
extern crate alloc;
mod cache;
use cache::{Cache, Stat, CACHE_T};
use libos32gui::{App, Ui, Window, WindowSpec};
use os32api::{api, kprint, KernelAPI};
use os32api::gui::types::Rect;

// Single cooperative task. The hook runs outside App callbacks.
static mut CACHE: Cache = Cache::new();
static mut SLOW: u32 = 0;
static mut STOP_FOCUS: bool = false;
static mut WINDOW: Option<Window> = None;

#[no_mangle]
pub extern "C" fn main(argc: i32, argv: *const *const u8, kapi: *mut KernelAPI) -> i32 {
    os32api::os32_init(kapi);
    let (slow, stop_focus) = match arguments(argc, argv) {
        Some(options) => options,
        None => { kprint!(b"usage: trim_back_rs [--slow-hook N] [--stop-focus]\n\0"); return 1; }
    };
    unsafe { SLOW = slow; STOP_FOCUS = stop_focus; }
    if libos32gui::init(kapi).is_err() { return prep_fail(); }
    let window = match Window::create(&WindowSpec::new(
        b"trim_back_rs: a arm / o observe / r regrow / q quit", Rect::new(30, 50, 360, 100))) {
        Ok(w) => w, Err(_) => return prep_fail(),
    };
    let c = unsafe { &mut *core::ptr::addr_of_mut!(CACHE) };
    if c.prepare().is_none() { return prep_fail(); }
    kprint!(b"trim_back_rs PREP OK id=%u ARENA=2 LARGE=0 cur=%x cache-T=%u cache-E=%u CRC=%x\n\0",
        c.before.id(), c.before.cur(), CACHE_T as u32, (c.count - c.keep - CACHE_T) as u32, c.crc);
    kprint!(b"trim_back_rs keys: a=arm o=observe G-4 r=regrow after observation q=quit\n\0");
    unsafe { WINDOW = Some(window); }
    libos32gui::set_trim_hook(hook);
    let rc = libos32gui::run(&mut Back);
    observe();
    let c = unsafe { &*core::ptr::addr_of!(CACHE) };
    let (retry, ok, hint) = os32api::retry_stats();
    kprint!(b"trim_back_rs exit RETRY_COUNT=%u RETRY_OK_COUNT=%u RETRY_HOOK_PAGES=%u hook_calls=%u CRC=%s regrow rc=%d pages=%u\n\0",
        retry, ok, hint, c.hook_calls, status(!c.bad && c.data_crc() == c.crc), c.regrow_rc, c.pages);
    let pass = rc.is_ok() && c.passed();
    unsafe { drop((*core::ptr::addr_of_mut!(WINDOW)).take()); }
    if pass { 0 } else { 1 }
}
fn status(ok: bool) -> *const u8 { if ok { b"OK\0".as_ptr() } else { b"BAD\0".as_ptr() } }
fn prep_fail() -> i32 { kprint!(b"trim_back_rs PREP FAIL\n\0"); 1 }
fn arguments(argc: i32, argv: *const *const u8) -> Option<(u32, bool)> {
    if argc == 1 { return Some((0, false)); }
    if argc < 1 || argv.is_null() { return None; }
    let mut slow = None;
    let mut stop_focus = false;
    let mut i = 1;
    unsafe {
        while i < argc as usize {
            let flag = *argv.add(i);
            if argument_is(flag, b"--stop-focus\0") && !stop_focus {
                stop_focus = true;
            } else if argument_is(flag, b"--slow-hook\0") && slow.is_none() && i + 1 < argc as usize {
                i += 1;
                let mut p = *argv.add(i);
                if p.is_null() || *p == 0 { return None; }
                let mut n = 0u32;
                while *p != 0 {
                    if !(*p).is_ascii_digit() { return None; }
                    n = n.checked_mul(10)?.checked_add((*p - b'0') as u32)?;
                    p = p.add(1);
                }
                if n > i32::MAX as u32 { return None; }
                slow = Some(n);
            } else { return None; }
            i += 1;
        }
    }
    Some((slow.unwrap_or(0), stop_focus))
}
unsafe fn argument_is(p: *const u8, expected: &[u8]) -> bool {
    !p.is_null() && expected.iter().enumerate().all(|(i, c)| *p.add(i) == *c)
}
fn hook() -> u32 {
    let c = unsafe { &mut *core::ptr::addr_of_mut!(CACHE) };
    if !c.armed || c.released { return 0; }
    if unsafe { STOP_FOCUS } {
        let window = unsafe { &*core::ptr::addr_of!(WINDOW) };
        let rc = window.as_ref().map_or(-1, |w| w.set_focus().map_or_else(|e| e.code(), |_| 0));
        if rc != 0 {
            kprint!(b"trim_back_rs focus FAIL rc=%d\n\0", rc);
            c.bad = true;
            return 0;
        }
        kprint!(b"trim_back_rs focus ok\n\0");
    }
    let slow = unsafe { SLOW };
    kprint!(b"trim_back_rs hook begin slow=%u\n\0", slow);
    let start = unsafe { (api().get_tick)() };
    while unsafe { (api().get_tick)() }.wrapping_sub(start) < slow {}
    let pages = c.release();
    kprint!(b"trim_back_rs DATA %s CRC=%x\n\0", status(!c.bad), c.data_crc());
    pages
}
fn observe() {
    let c = unsafe { &mut *core::ptr::addr_of_mut!(CACHE) };
    if c.released && !c.observed {
        let ok = c.observe_done();
        kprint!(b"trim_back_rs DONE pages=%u pending=%u\n\0", c.pages, c.trimmed.pending() as u32);
        kprint!(b"trim_back_rs G-4 %s cur=%x->%x ARENA=%u->%u INITIAL=%u->%u DATA=%s; r=regrow after observation\n\0",
            if ok { b"OK\0".as_ptr() } else { b"FAIL\0".as_ptr() }, c.before.cur(), c.trimmed.cur(),
            c.before.arenas(), c.trimmed.arenas(), c.initial.initial(), c.trimmed.initial(), status(!c.bad));
    }
}
struct Back;
impl App for Back {
    fn after_commit(&mut self, _: &mut Ui) { observe(); }
    fn on_key(&mut self, ui: &mut Ui, _: u32, _: u8, ch: u8, _: u8, down: bool) {
        if !down { return; }
        let was_observed = unsafe { (*core::ptr::addr_of!(CACHE)).observed };
        observe();
        let c = unsafe { &mut *core::ptr::addr_of_mut!(CACHE) };
        match ch {
            b'a' => { c.armed = true; kprint!(b"trim_back_rs arm accepted\n\0"); }
            b'r' => {
                if !was_observed && c.observed {
                    kprint!(b"trim_back_rs inspect G-4, then press r for regrow\n\0");
                    return;
                }
                let ok = c.regrow();
                kprint!(b"trim_back_rs obstacle unmapped rc=%d pages=1 (excluded from trim pages)\n\0", c.unmap_rc);
                let cur = Stat::read().map_or(0, |s| s.cur());
                kprint!(b"trim_back_rs regrow %s rc=%d cur=%x->%x ARENA=1 EXACT\n\0",
                    if ok { b"ok\0".as_ptr() } else { b"FAIL\0".as_ptr() }, c.regrow_rc, c.trimmed.cur(), cur);
                if !ok { ui.quit(); }
            }
            b'q' => ui.quit(),
            _ => {}
        }
    }
}
