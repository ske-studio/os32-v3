// Execute the guest Rust arguments/hook with deterministic transport seams.
#![allow(dead_code, static_mut_refs)]
use std::sync::Mutex;
static LOG: Mutex<Vec<&'static str>> = Mutex::new(Vec::new());
static mut FOCUS_ERROR: bool = false;
static mut TICK: u32 = 0;
struct Error;
impl Error { fn code(&self) -> i32 { -1 } }
struct Window;
impl Window {
    fn set_focus(&self) -> Result<(), Error> {
        LOG.lock().unwrap().push("focus");
        if unsafe { FOCUS_ERROR } { Err(Error) } else { Ok(()) }
    }
}
struct Cache { armed: bool, released: bool, bad: bool }
impl Cache {
    const fn new() -> Self { Self { armed: false, released: false, bad: false } }
    fn release(&mut self) -> u32 { LOG.lock().unwrap().push("release"); self.released = true; 1 }
    fn data_crc(&self) -> u32 { 0 }
}
static mut CACHE: Cache = Cache::new();
static mut STOP_FOCUS: bool = false;
static mut WINDOW: Option<Window> = None;
static mut SLOW: u32 = 3;
struct Api { get_tick: unsafe fn() -> u32 }
unsafe fn tick() -> u32 { LOG.lock().unwrap().push("tick"); let n = TICK; TICK += 1; n }
fn api() -> &'static Api { static API: Api = Api { get_tick: tick }; &API }
macro_rules! kprint { ($fmt:expr $(, $arg:expr)* $(,)?) => {{ $(let _ = $arg;)*
    let fmt: &[u8] = $fmt;
    let label = if fmt.windows(8).any(|w| w == b"focus ok") { "ok" }
        else if fmt.windows(10).any(|w| w == b"focus FAIL") { "error" }
        else { "print" };
    LOG.lock().unwrap().push(label);
}} }
fn status(_: bool) -> *const u8 { b"OK\0".as_ptr() }
// INSERT_GUEST_FUNCTIONS
#[test]
fn rust_stop_focus_before_hook_and_error_stops() {
    for (focus, error) in [(false, false), (true, false), (true, true)] {
        LOG.lock().unwrap().clear();
        unsafe {
            CACHE = Cache { armed: true, released: false, bad: false };
            STOP_FOCUS = focus; FOCUS_ERROR = error; WINDOW = Some(Window); TICK = 0;
        }
        let result = hook();
        let log = LOG.lock().unwrap();
        if error {
            assert_eq!(&*log, &["focus", "error"], "stop-focus error stops before ticks/release");
            assert!(unsafe { CACHE.bad && !CACHE.released }, "stop-focus error marks failure");
            assert_eq!(result, 0);
        } else {
            if focus { assert_eq!(&log[..2], &["focus", "ok"], "stop-focus confirmed before hook"); }
            else { assert!(!log.contains(&"focus"), "default does not focus"); }
            assert_eq!(result, 1);
            assert!(unsafe { CACHE.released && !CACHE.bad });
        }
    }
    let flags = [b"app\0".as_ptr(), b"--stop-focus\0".as_ptr(), b"--slow-hook\0".as_ptr(), b"3\0".as_ptr()];
    assert_eq!(arguments(4, flags.as_ptr()), Some((3, true)), "stop-focus CLI accepted");
    let repeated = [flags[0], flags[1], flags[1]];
    assert_eq!(arguments(3, repeated.as_ptr()), None, "stop-focus repeated CLI refused");
}
