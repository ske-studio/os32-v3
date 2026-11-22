//! WM observation and releases owed to the original input recipient.
use crate::{input, ring, wm};
use os32api::gui::proto::*;

pub const KBD: u32 = 1;
pub const MOUSE: u32 = 2;

#[derive(Clone, Copy)]
struct Delivery {
    window: u32,
    owner: i32,
    keys: [u8; 16],
    mods: [u8; 128],
    buttons: u8,
    retry_buttons: u8,
    pointer: bool,
    origin: [(i32, i32); 8],
    xy: [(i16, i16); 8],
    pointer_xy: (i16, i16),
}

impl Delivery {
    const EMPTY: Self = Self {
        window: 0, owner: 0, keys: [0; 16], mods: [0; 128],
        buttons: 0, retry_buttons: 0, pointer: false, origin: [(0, 0); 8], xy: [(0, 0); 8], pointer_xy: (0, 0),
    };
}

#[derive(Clone, Copy, PartialEq)]
struct Sample {
    window: u32,
    owner: i32,
    generation: u32,
    flags: u32,
}

pub struct State {
    sample: Option<Sample>,
    pub observed: u32,
    pub settled: u32,
    queried: bool,
    focus_confirmed: bool,
    valid: bool,
    pub mouse_baseline: bool,
    pub held_buttons: u8,
    prepared: u32,
    purge_same_owner: bool,
    delivered: [Delivery; GUI_MAX_WINDOWS],
}

impl State {
    pub const NEW: Self = Self {
        sample: None, observed: 0, settled: 0, queried: false, focus_confirmed: false, valid: false, mouse_baseline: false, held_buttons: 0, prepared: 0, purge_same_owner: false,
        delivered: [Delivery::EMPTY; GUI_MAX_WINDOWS],
    };

    pub fn cursor_allowed(&self) -> bool {
        !self.queried || self.open(MOUSE)
    }

    pub fn open(&self, bit: u32) -> bool {
        self.valid && self.observed == self.settled && self.focus_confirmed &&
            self.sample.is_some_and(|s| s.flags & bit == 0)
    }
}

pub fn observe(st: &mut wm::GuiState) {
    let mut out = os32api::av::IN2_WmState { size: 0, gen: 0, excl: 0, owner: 0 };
    st.excl.queried = true;
    st.excl.valid = unsafe { (os32api::api().in2_wm_state)(&mut out) } == 0;
    if st.excl.valid {
        let next = Sample { window: st.front_id(), owner: st.front_owner(), generation: out.gen, flags: out.excl };
        if let Some(old) = st.excl.sample.filter(|old| *old != next) {
            st.excl.observed = st.excl.observed.wrapping_add(1);
            st.excl.mouse_baseline = true;
            // An owner transition purges kernel raw itself. Only scan the
            // kernel queue for window changes within the current owner.
            st.excl.purge_same_owner = old.owner == next.owner &&
                (st.excl.purge_same_owner || old.window != next.window);
        }
        st.excl.focus_confirmed = crate::multiapp::av_owner() == next.owner;
        st.excl.sample = Some(next);
    }
    if !st.excl.open(MOUSE) && st.cursor.shown {
        let rect = crate::cursor::rect(st);
        crate::cursor::hide(st);
        wm::queue_present(st, rect);
        wm::flush_present();
    }
}

// Decode the three index bits of a one-hot button mask on the 386.
fn button_slot(button: u8) -> usize {
    let bit = button & button.wrapping_neg();
    usize::from(bit & 0xaa != 0)
        | (usize::from(bit & 0xcc != 0) << 1)
        | (usize::from(bit & 0xf0 != 0) << 2)
}

// A capture can outlive settlement or window destruction. Only a delivered,
// still-held button has an origin to use for a physical release.
pub fn button_origin(st: &wm::GuiState, window: u32, button: u8) -> Option<(i32, i32)> {
    let index = st.win_by_id(window)?;
    let rec = &st.excl.delivered[index];
    if rec.window == window && rec.owner == st.windows[index].owner && rec.buttons & button != 0 {
        Some(rec.origin[button_slot(button)])
    } else {
        None
    }
}

pub fn append(st: &mut wm::GuiState, slot: usize, ev: &GuiEvent) -> bool {
    // Some synthetic mouse events enter directly through the WM.
    if st.excl.sample.is_none() { observe(st); }
    let device = if ev.kind == GUI_EV_KEY { KBD } else { MOUSE };
    if !st.excl.open(device) { return false; }
    let index = match st.win_by_id(ev.window) { Some(i) => i, None => return false };
    let owner = st.windows[index].owner;
    if st.slot_of_owner(owner) != Some(slot) { return false; }
    let rec = &st.excl.delivered[index];
    let scan = ev.payload[0] as usize;
    if ev.kind == GUI_EV_KEY && ev.sub == 0 &&
        (rec.window != ev.window || rec.owner != owner || rec.keys[scan / 8] & (1 << (scan % 8)) == 0) {
        return false;
    }
    if rec.window == ev.window && rec.owner == owner && rec.retry_buttons != 0 && device == MOUSE && !(ev.kind == GUI_EV_BUTTON && ev.sub == 0) {
        return false;
    }
    let accepted = ring::append(st, slot, ev);
    if !accepted {
        if ev.kind == GUI_EV_BUTTON && ev.sub == 0 && rec.window == ev.window && rec.owner == owner {
            let rec = &mut st.excl.delivered[index];
            let bit = ev.payload[4];
            if bit.count_ones() == 1 && rec.buttons & bit != 0 {
                rec.retry_buttons |= bit;
                rec.xy[button_slot(bit)] = (i16::from_le_bytes([ev.payload[0], ev.payload[1]]),
                    i16::from_le_bytes([ev.payload[2], ev.payload[3]]));
            }
        }
        return false;
    }
    let rec = &mut st.excl.delivered[index];
    if rec.window != ev.window || rec.owner != owner {
        *rec = Delivery { window: ev.window, owner, ..Delivery::EMPTY };
    }
    if ev.kind == GUI_EV_KEY {
        let bit = 1 << (scan % 8);
        if ev.sub != 0 {
            rec.keys[scan / 8] |= bit;
            rec.mods[scan] = ev.payload[2];
        } else {
            rec.keys[scan / 8] &= !bit;
        }
    } else {
        let xy = (i16::from_le_bytes([ev.payload[0], ev.payload[1]]),
                  i16::from_le_bytes([ev.payload[2], ev.payload[3]]));
        let previously_down = rec.buttons;
        if ev.kind == GUI_EV_POINTER {
            rec.buttons = ev.sub;
            rec.pointer = true;
            rec.pointer_xy = xy;
        } else if ev.sub != 0 {
            rec.buttons |= ev.payload[4];
        } else {
            rec.buttons &= !ev.payload[4];
            rec.retry_buttons &= !ev.payload[4];
        }
        let positioned = if ev.kind == GUI_EV_POINTER { ev.sub } else { ev.payload[4] };
        for i in 0..8 {
            let bit = 1 << i;
            if positioned & bit != 0 {
                rec.xy[i] = xy;
                if (rec.buttons & !previously_down) & bit != 0 || (ev.kind == GUI_EV_BUTTON && ev.sub != 0) {
                    rec.origin[i] = st.windows[index].client_origin();
                }
            }
        }
    }
    accepted
}

// No ordinary append while observed differs from settled. Thus this table also
// serves as the frozen retry queue, even across another generation change.
pub fn settle(st: &mut wm::GuiState) {
    if !st.excl.valid { return; }
    if !st.excl.focus_confirmed {
        crate::multiapp::sync_av_focus(st);
        observe(st);
        if !st.excl.valid || !st.excl.focus_confirmed { return; }
    }
    if st.excl.observed == st.excl.settled {
        for i in 0..GUI_MAX_WINDOWS {
            if st.excl.delivered[i].retry_buttons != 0 { release_window(st, i, false); }
        }
        return;
    }
    if st.excl.prepared != st.excl.observed {
        input::purge_boundary(st, st.excl.purge_same_owner);
        st.excl.purge_same_owner = false;
        input::cancel_capture(st);
        st.excl.prepared = st.excl.observed;
    }
    let mut complete = true;
    for i in 0..GUI_MAX_WINDOWS {
        complete &= release_window(st, i, true);
    }
    if complete { st.excl.settled = st.excl.observed; }
}

fn release_window(st: &mut wm::GuiState, i: usize, boundary: bool) -> bool {
    let rec = st.excl.delivered[i];
    if rec.window == 0 { return true; }
    if st.win_by_id(rec.window) != Some(i) || st.windows[i].owner != rec.owner {
        st.excl.delivered[i] = Delivery::EMPTY;
        return true;
    }
    let Some(slot) = st.slot_of_owner(rec.owner) else {
        st.excl.delivered[i] = Delivery::EMPTY;
        return true;
    };
    for scan in 0..128 {
        let bit = 1 << (scan % 8);
        if boundary && rec.keys[scan / 8] & bit != 0 {
            let ev = ring::ev_key(false, rec.window, scan as u8, 0, rec.mods[scan], input::next_serial(st, slot));
            if !ring::append(st, slot, &ev) { return false; }
            st.excl.delivered[i].keys[scan / 8] &= !bit;
        }
    }
    let buttons = if boundary { rec.buttons } else { rec.retry_buttons };
    for b in 0..8 {
        let bit = 1 << b;
        if buttons & bit != 0 {
            let (x, y) = rec.xy[b];
            let ev = ring::ev_button(false, rec.window, x, y, bit, input::next_serial(st, slot));
            if !ring::append(st, slot, &ev) { return false; }
            st.excl.delivered[i].buttons &= !bit;
            st.excl.delivered[i].retry_buttons &= !bit;
        }
    }
    if !boundary { return true; }
    if rec.pointer {
        let (x, y) = rec.pointer_xy;
        let ev = ring::ev_pointer(rec.window, x, y, 0, input::next_serial(st, slot));
        if !ring::append(st, slot, &ev) { return false; }
    }
    st.excl.delivered[i] = Delivery::EMPTY;
    true
}
