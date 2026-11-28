// Model only the event sink, focus sample and window identities.
extern crate self as os32api;
pub mod gui {
    pub mod proto {
        pub const GUI_MAX_WINDOWS: usize = 2;
        pub const GUI_EV_KEY: u8 = 1;
        pub const GUI_EV_BUTTON: u8 = 2;
        pub const GUI_EV_POINTER: u8 = 3;
        pub const GUI_COLOR_TEXT: u8 = 1;
        pub const GUI_COLOR_WINDOW: u8 = 2;
        pub const GUI_EV_TEXT: u8 = 4;
        #[derive(Copy, Clone)]
        pub struct GuiEvent { pub window: u32, pub kind: u8, pub sub: u8, pub payload: [u8; 8] }
    }
}
use gui::proto::*;
thread_local! {
    static RAW: std::cell::RefCell<std::collections::VecDeque<i32>> = Default::default();
    static FLAGS: std::cell::Cell<u32> = Default::default();
}
pub mod av {
    #[allow(non_camel_case_types)]
    pub struct IN2_WmState { pub size: u32, pub gen: u32, pub excl: u32, pub owner: i32 }
}
pub struct Api {
    pub gfx_screen_owner: unsafe fn() -> i32,
    pub kbd_inject: unsafe fn(*const u8, i32) -> i32,
    pub kbd_dropped_count: unsafe fn() -> u32,
    pub exec_abort_clear: unsafe fn() -> i32,
    pub get_tick: unsafe fn() -> u32,
    pub kbd_trygetrawkey: unsafe fn() -> i32,
    pub kbd_is_pressed: unsafe fn(i32) -> i32,
    pub in2_wm_state: unsafe fn(&mut av::IN2_WmState) -> i32,
}
pub fn api() -> Api {
    Api {
        gfx_screen_owner: || unreachable!(),
        kbd_inject: |_, _| unreachable!(),
        kbd_dropped_count: || 0,
        exec_abort_clear: || { multiapp::STOP_TRACE.with(|t| t.borrow_mut().push(0)); 0 },
        get_tick: || 0,
        kbd_trygetrawkey: || RAW.with(|q| q.borrow_mut().pop_front().unwrap_or(-1)),
        kbd_is_pressed: |_| 0,
        in2_wm_state: |s| { s.excl = FLAGS.get(); 0 },
    }
}
#[path = "../../../../userland/gshell/src/excl.rs"]
mod excl;
mod wm {
    use super::*;
    use std::cell::{Cell, RefCell};
    pub struct Window { pub owner: i32, pub id: u32 }
    impl Window {
        pub fn id(&self, _: usize) -> u32 { self.id }
        pub fn client_origin(&self) -> (i32,i32) { (0,0) }
    }
    pub struct Rect;
    impl Rect {
        pub fn new(_: i32, _: i32, _: i32, _: i32) -> Rect { Rect }
        pub fn intersects(&self, _: &Rect) -> bool { unreachable!() }
    }
    pub struct GuiState {
        pub inited: bool, pub in_pump: bool, pub now: u32, pub last_kbd_dropped: u32,
        pub excl: excl::State, pub windows: [Window; 2], pub cursor: cursor::Cursor, pub screen_w: i32, pub screen_h: i32,
        pub pending_raw: [i32;128], pub pending_rank: [u8;128], pub pending_raw_n: usize,
        pub pending_tab: bool, pub pending_switch: bool, pub pending_raw_dropped: u32,
        pub held_keys: [bool;128], pub stop_suppressed: bool, pub stop_tick: u32,
        pub abort_seen: bool, pub switching: bool, pub switches: usize,
        pub drag_index: i32, pub prev_buttons: u8, pub click_button: u8, pub delivered_button: u8,
        pub front: usize, pub space: Cell<usize>, pub events: RefCell<Vec<GuiEvent>>,
    }
    impl GuiState {
        pub fn front_index(&self) -> Option<usize> { Some(self.front) }
        pub fn front_id(&self) -> u32 { self.windows[self.front].id }
        pub fn front_owner(&self) -> i32 { self.windows[self.front].owner }
        pub fn win_by_id(&self, id: u32) -> Option<usize> { self.windows.iter().position(|w| w.id == id) }
        pub fn slot_of_owner(&self, owner: i32) -> Option<usize> { self.windows.iter().position(|w| w.owner == owner) }
    }
    #[cfg(legacy_gui)]
    pub fn g() -> &'static mut GuiState {
        // Same single-threaded, non-reentrant entry as the registered WM pump.
        static mut STATE: *mut GuiState = std::ptr::null_mut();
        unsafe {
            if STATE.is_null() { STATE = Box::into_raw(Box::new(window_state())); }
            &mut *STATE
        }
    }
    pub fn drop_drag_frame(st: &mut GuiState) { st.drag_index = -1; }
    pub fn queue_present(_: &mut GuiState, _: Rect) {}
    pub fn flush_present() {}
}
#[path = "../../../../userland/gshell/src/cursor.rs"]
mod cursor;
mod gfx {
    thread_local! { pub static PIXELS: std::cell::RefCell<[u8; 256]> = const { std::cell::RefCell::new([0; 256]) }; }
    pub unsafe fn gfx_pixel(x: i32, y: i32, color: u8) {
        PIXELS.with(|p| p.borrow_mut()[(y * 16 + x) as usize] = color);
    }
}
mod ffi {
    pub unsafe fn gfx_get_pixel(x: i32, y: i32) -> u8 {
        super::gfx::PIXELS.with(|p| p.borrow()[(y * 16 + x) as usize])
    }
}
#[no_mangle]
static gfx_direct: i32 = 0;
// The fixture selects the software framebuffer. A direct command is unexpected.
#[no_mangle]
extern "C" fn gfx_cmd_get(_:i32, _:i32, _:i32, _:i32, _: *mut u8, _:u32) -> i32 { unreachable!() }
#[no_mangle]
extern "C" fn gfx_cmd_enqueue_put(_:i32, _:i32, _:i32, _:i32, _: *const u8, _:u32, _:i32) -> i32 { unreachable!() }
#[no_mangle]
extern "C" fn gfx_cmd_enqueue_mono(_:i32, _:i32, _:i32, _:i32, _: *const u8, _:u32, _:u8,
                                   _:i32, _:i32, _:i32, _:i32, _:i32) -> i32 { unreachable!() }
mod multiapp {
    use std::sync::atomic::{AtomicI32, Ordering};
    pub static OWNER: AtomicI32 = AtomicI32::new(2);
    pub static FAIL_NEXT: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
    const APP_ID_SHELL: i32 = 1;
    pub struct Multi { pub abort_clear_req: bool }
    fn m() -> Multi { Multi { abort_clear_req: false } }
    thread_local! { pub static STOP_TRACE: std::cell::RefCell<Vec<i32>> = Default::default(); }
    fn abort_target(st: &super::wm::GuiState) -> i32 { st.front_owner() }
    fn kill_for_request(id: i32) { STOP_TRACE.with(|t| t.borrow_mut().push(id)); }
    pub fn redirect_abort(_: &super::wm::GuiState, _: i32) { unreachable!() }
    mod stop_dispatch { include!("../../../../userland/gshell/src/stop_dispatch.rs"); }
    pub(crate) use stop_dispatch::top_level_abort;
    pub fn chain_tail(owner: i32) -> i32 { owner }
    pub fn note_injected_key(_: i32) { unreachable!() }
    static WINDOW: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(10);
    pub fn av_window() -> u32 { WINDOW.load(Ordering::Relaxed) }
    pub fn av_owner() -> i32 { OWNER.load(Ordering::Relaxed) }
    pub fn sync_av_focus(st: &mut super::wm::GuiState) {
        if !FAIL_NEXT.swap(false, Ordering::Relaxed) {
            OWNER.store(st.front_owner(), Ordering::Relaxed);
            WINDOW.store(st.front_id(), Ordering::Relaxed);
        }
    }
}
mod fullscreen {
    pub fn owner() -> i32 { unreachable!() }
    pub fn active() -> bool { false }
}
mod modal {
    pub fn is_input() -> bool { false }
    pub fn on_key(_: &mut super::GuiState, _: u8, _: u8, _: u32) { unreachable!() }
    pub fn is_open() -> bool { false }
}
mod startmenu {
    pub fn on_key(_: &mut super::GuiState, _: u8) { unreachable!() }
    pub fn is_open() -> bool { false }
}
mod fep {
    use super::GuiState;
    #[derive(PartialEq)]
    pub enum Fed { Pass, Consumed }
    pub fn feed(_: &mut GuiState, _: u8, _: u8, _: u32) -> Fed { Fed::Pass }
    pub fn flush_text(_: &mut GuiState) {}
    pub fn toggle(_: &mut GuiState) { unreachable!() }
    pub fn request_toggle() { unreachable!() }
    pub fn apply_pending_commit(_: &mut GuiState) { unreachable!() }
    pub fn apply_pending_toggle(_: &mut GuiState) { unreachable!() }
    pub fn is_on() -> bool { false }
    pub fn toggle_pending() -> bool { false }
}
mod kbdnav {
    use super::wm::GuiState;
    pub fn pre(_: &mut GuiState, _: u8, _: bool, _: u32) -> bool { unreachable!() }
    pub fn is_wm_raw(_: &GuiState, raw: i32) -> bool { matches!(raw & 127, 15 | 96 | 115) }
    pub fn pump_delivered_make(_: &mut GuiState, _: u8) {}
    pub fn on_key(st: &mut GuiState, _: u8, down: bool, _: u32) -> bool { st.switching = down; true }
    pub fn finish_if_released(st: &mut GuiState, _: u8, _: bool, mods: u32) -> bool {
        if !st.switching || mods & 8 != 0 { return false; }
        st.switching = false;
        st.switches += 1;
        true
    }
}
#[path = "../../../../userland/gshell/src/raw_keyboard.rs"]
mod raw_keyboard;
use wm::GuiState;
const SC_STOP: u8 = 0x60;
const SC_CTRL: u8 = 0x74;
const SC_SHIFT: u8 = 0x70;
const SC_TAB: u8 = 15;
const SC_GRPH: u8 = 0x73;
const MOD_CTRL: u32 = 16;
const MOD_GRPH: u32 = 8;
const STOP_SUPPRESS_TICKS: u32 = 50;
#[derive(Copy, Clone, PartialEq, Eq)]
pub enum Ctx { Pump, Wait, Standalone }
impl Ctx { fn wm_ui(self) -> bool { self != Ctx::Pump } }
#[cfg(legacy_gui)]
mod diagnostics { pub fn shortcut(_: u8, _: u32) -> bool { unreachable!() } }
#[cfg(legacy_gui)]
mod gserial { pub fn lost(_: &mut super::GuiState) { unreachable!() } }
mod input {
    #[cfg(legacy_gui)]
    use super::{fep, kbdnav, modal, raw_keyboard, ring, startmenu, SC_SHIFT, SC_CTRL, SC_STOP, MOD_CTRL};
    #[cfg(legacy_gui)]
    pub use super::Ctx;
    #[cfg(legacy_gui)]
    include!(env!("WM_KEYBOARD_ITEMS"));
    #[cfg(legacy_gui)]
    fn standalone_key(_: &mut GuiState, _: u8) { unreachable!() }
    #[cfg(legacy_gui)]
    fn capture_mouse(_: &mut GuiState, _: Ctx, _: bool) {}
    use super::wm::GuiState;
    pub(crate) use super::raw_keyboard::purge_boundary;
    pub fn cancel_capture(_: &mut GuiState) {}
    pub fn next_serial(_: &mut GuiState, _: usize) -> u16 { 0 }
}
mod ring {
    use super::*;
    pub fn space(st: &GuiState, _: usize) -> usize { st.space.get() }
    pub fn add_dropped(_: &mut GuiState, _: usize, _: u16) { unreachable!() }
    pub fn append(st: &wm::GuiState, slot: usize, ev: &GuiEvent) -> bool {
        #[cfg(legacy_gui)]
        assert_eq!(slot, st.front);
        if st.space.get() == 0 { return false; }
        st.space.set(st.space.get()-1);
        st.events.borrow_mut().push(*ev);
        true
    }
    pub fn ev_key(down: bool, window: u32, scan: u8, _: u8, _: u8, _: u16) -> GuiEvent {
        let mut ev = GuiEvent {window, kind: GUI_EV_KEY, sub: down as u8, payload: [0;8]};
        ev.payload[0] = scan;
        ev
    }
    pub fn ev_text(window: u32, payload: [u8; 8], sub: u8, _: u16) -> GuiEvent {
        GuiEvent { window, kind: GUI_EV_TEXT, sub, payload }
    }
    pub fn ev_pointer(window: u32, _: i16, _: i16, sub: u8, _: u16) -> GuiEvent {
        GuiEvent {window, kind: GUI_EV_POINTER, sub, payload: [0;8]}
    }
    pub fn ev_button(down: bool, window: u32, _: i16, _: i16, button: u8, _: u16) -> GuiEvent {
        let mut ev = GuiEvent {window, kind: GUI_EV_BUTTON, sub: down as u8, payload: [0;8]};
        ev.payload[4] = button;
        ev
    }
}
fn window_state() -> wm::GuiState {
    wm::GuiState {
        inited: true, in_pump: false, now: 0, last_kbd_dropped: 0,
        excl: excl::State::NEW, windows: [wm::Window {owner: 2, id: 10}, wm::Window {owner: 3, id: 11}],
        pending_raw: [0;128], pending_rank: [0;128], pending_raw_n: 0,
        pending_tab: false, pending_switch: false, pending_raw_dropped: 0,
        held_keys: [false;128], stop_suppressed: false, stop_tick: 0,
        abort_seen: false, switching: false, switches: 0,
        drag_index: -1, prev_buttons: 0, click_button: 0, delivered_button: 0,
        front: 0, cursor: cursor::Cursor::EMPTY, screen_w: 16, screen_h: 16, space: 2.into(), events: Default::default(),
    }
}
#[cfg(not(legacy_gui))]
fn main() {
    let mut st = window_state();
    assert!(excl::append(&mut st, 0, &ring::ev_key(true, 10, 42, 0, 0, 0)));
    assert!(excl::append(&mut st, 0, &ring::ev_pointer(10, 0, 0, 1, 0)));
    st.front = 1;
    multiapp::FAIL_NEXT.store(true, std::sync::atomic::Ordering::Relaxed);
    excl::observe(&mut st);
    excl::settle(&mut st);
    assert_eq!(multiapp::av_owner(), 2);
    st.space.set(1);
    assert!(!excl::append(&mut st, 1, &ring::ev_key(true, 11, 42, 0, 0, 0)));
    st.space.set(0);
    excl::observe(&mut st);
    excl::settle(&mut st);
    assert!(!st.excl.open(excl::KBD));
    assert_eq!(multiapp::av_owner(), 3);
    for expected in [GUI_EV_KEY, GUI_EV_BUTTON, GUI_EV_POINTER] {
        st.space.set(1);
        excl::settle(&mut st);
        let events = st.events.borrow();
        let last = events.last().unwrap();
        assert_eq!((last.window,last.kind,last.sub),(10,expected,0));
    }
    assert!(st.excl.open(excl::KBD));
    assert_eq!(st.events.borrow().len(),5);
    // A failed make creates no release obligation. Reuse the slot of a
    // successful make before settlement and leave the replacement untouched.
    assert!(!excl::append(&mut st, 1, &ring::ev_key(true, 11, 51, 0, 0, 0)));
    st.space.set(1);
    assert!(excl::append(&mut st, 1, &ring::ev_key(true, 11, 52, 0, 0, 0)));
    raw_keyboard::defer_raw(&mut st, i32::from(SC_TAB) | 256);
    st.windows[1].id = 12;
    multiapp::FAIL_NEXT.store(true, std::sync::atomic::Ordering::Relaxed);
    excl::observe(&mut st);
    excl::settle(&mut st);
    assert!(!st.excl.open(excl::KBD));
    st.space.set(8);
    excl::observe(&mut st);
    excl::settle(&mut st);
    assert!(st.excl.open(excl::KBD));
    assert_eq!(st.pending_raw_n, 0);
    assert_eq!(st.events.borrow().len(),6);

    // A full deferred queue and a full window ring must still admit WM controls.
    FLAGS.set(excl::KBD as u32);
    excl::observe(&mut st);
    excl::settle(&mut st);
    st.space.set(0);
    for _ in 0..128 { raw_keyboard::defer_raw(&mut st, 42 | 256); }
    let start = i32::from(SC_TAB) | 256 | ((MOD_GRPH as i32) << 9);
    let finish = i32::from(SC_GRPH);
    let stop = i32::from(SC_STOP) | 256 | ((MOD_CTRL as i32) << 9);
    RAW.with(|q| q.borrow_mut().extend([start, finish, start, finish, stop]));
    let mut captured = false;
    for _ in 0..5 {
        assert!(matches!(raw_keyboard::next(&mut st, Ctx::Pump, false, &mut captured),
                         raw_keyboard::Step::Consumed));
    }
    assert!(captured);
    for _ in 0..128 {
        match raw_keyboard::next(&mut st, Ctx::Wait, false, &mut captured) {
            raw_keyboard::Step::End => break,
            raw_keyboard::Step::Consumed => (),
            raw_keyboard::Step::Deliver(scan, down, _) => {
                assert!(!excl::append(&mut st, 1, &ring::ev_key(down, 12, scan, 0, 0, 0)));
            }
        }
    }
    assert_eq!(st.switches, 2);
    assert!(st.abort_seen);
    assert_eq!(multiapp::top_level_abort(&mut st), 3);
    assert_eq!(multiapp::top_level_abort(&mut st), 0);
    multiapp::STOP_TRACE.with(|trace| assert_eq!(trace.borrow().as_slice(), &[0, 3]));
    assert_eq!(st.events.borrow().len(), 6);
    st.space.set(8);
    for flags in [excl::KBD as u32, 0] {
        FLAGS.set(flags);
        excl::observe(&mut st);
        excl::settle(&mut st);
        RAW.with(|q| q.borrow_mut().push_back(42 | 256));
        if let raw_keyboard::Step::Deliver(scan, down, _) =
            raw_keyboard::next(&mut st, Ctx::Wait, true, &mut captured) {
            excl::deliver_window_key(&mut st, 1, 12, scan, down, b'a', 0);
        }
        let events = st.events.borrow();
        let delivered = &events[6..];
        if flags == 0 {
            assert_eq!(delivered.len(), 2);
            assert_eq!((delivered[0].window, delivered[0].kind), (12, GUI_EV_KEY));
            assert_eq!((delivered[1].window, delivered[1].kind, delivered[1].payload[0]),
                       (12, GUI_EV_TEXT, b'a'));
        } else {
            assert!(delivered.is_empty());
        }
    }

    cursor::show(&mut st);
    assert_eq!(unsafe { ffi::gfx_get_pixel(0, 0) }, GUI_COLOR_TEXT);
    st.drag_index = 1;
    FLAGS.set(excl::MOUSE);
    excl::observe(&mut st);
    excl::settle(&mut st);
    assert_eq!(st.drag_index, -1);
    assert!(!st.cursor.shown);
    assert_eq!(unsafe { ffi::gfx_get_pixel(0, 0) }, 0);
    FLAGS.set(0);
    excl::observe(&mut st);
    excl::settle(&mut st);
    excl::restore_mouse_baseline(&mut st, 0, 0, 0);
    assert!(st.cursor.shown);
    assert_eq!(unsafe { ffi::gfx_get_pixel(0, 0) }, GUI_COLOR_TEXT);

}

#[cfg(legacy_gui)]
#[path = "../../../../userland/gshell/src/pump.rs"]
mod pump;

#[cfg(legacy_gui)]
fn main() {
    let raw: i32 = std::env::args().nth(1).unwrap().parse().unwrap();
    wm::g().space.set(8);
    RAW.with(|q| q.borrow_mut().push_back(raw));
    pump::gshell_gui_pump();
    pump::gshell_gui_pump();
    let events = wm::g().events.borrow();
    assert_eq!(events.len(), 2);
    let (key, text) = (&events[0], &events[1]);
    assert_eq!((key.window, key.kind, key.sub, key.payload[0]), (10, GUI_EV_KEY, 1, 0x1d));
    assert_eq!((text.window, text.kind, text.sub, text.payload[0]), (10, GUI_EV_TEXT, 1, b'a'));
}
