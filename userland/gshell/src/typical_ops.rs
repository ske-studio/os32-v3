//! P0a: input-to-present acceptance script, excluded from product images.
use core::fmt::{self, Write};
use os32api::gui::types::Stats as GfxStats;
use crate::{gserial, ring, wm::{self, GuiState}};

const NAMES: [&str; 6] = ["move", "scroll", "menu", "desktop", "char", "list"];
const MAX: usize = 300;
const WARM: u8 = 10;
const TIMEOUT_TICKS: u32 = 200;
const FAILED: u32 = u32::MAX;

pub struct Run {
    op: u8, warm: u8, left: u16, i: u16, t0: u64, deadline: u32,
    base: GfxStats, id: [u8; 8], pub active: bool,
    waiting: bool, phase: u8, last_commit: u32, valid: bool,
    origin: (i32, i32), window: u32, owner: i32,
    samples: [u32; MAX], count: usize,
}
impl Run {
    pub const NEW: Self = Self {
        op: 0, warm: 0, left: 0, i: 0, t0: 0, deadline: 0,
        base: GfxStats::ZERO, id: [0; 8], active: false,
        waiting: false, phase: 0, last_commit: 0, valid: true,
        origin: (0, 0), window: 0, owner: 0, samples: [0; MAX], count: 0,
    };
}

pub fn parse(arg: &[u8]) -> Option<(u8, u16)> {
    let mut words = arg.split(|b| *b == b' ');
    let name = words.next()?;
    let op = NAMES.iter().position(|s| s.as_bytes() == name)? as u8;
    let n = match words.next() {
        None => 50,
        Some(s) => {
            if s.is_empty() { return None; }
            let mut n = 0u16;
            for b in s {
                if !b.is_ascii_digit() { return None; }
                n = n.checked_mul(10)?.checked_add(u16::from(b-b'0'))?;
            }
            n
        }
    };
    if words.next().is_some() || n == 0 || usize::from(n) > MAX { return None; }
    Some((op, n))
}
fn now() -> Option<u64> {
    let (mut lo, mut hi) = (0, 0);
    if unsafe { (os32api::api().sys_time_now)(&mut lo, &mut hi) } != 0 { return None; }
    Some((u64::from(hi) << 32) | u64::from(lo))
}
fn stats() -> GfxStats {
    let mut s = GfxStats::ZERO;
    unsafe { (os32api::api().gfx_stats)(&mut s as *mut GfxStats as *mut u8); }
    s
}
struct Line { bytes: [u8; 200], len: usize }
impl Write for Line {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        if self.len + s.len() >= self.bytes.len() { return Err(fmt::Error); }
        self.bytes[self.len..self.len+s.len()].copy_from_slice(s.as_bytes());
        self.len += s.len(); Ok(())
    }
}
fn emit(args: fmt::Arguments<'_>) {
    let mut line = Line { bytes: [0; 200], len: 0 };
    if line.write_fmt(args).is_ok() {
        unsafe { (os32api::api().serial_puts)(line.bytes.as_ptr()); }
    }
}
fn title(st: &GuiState) -> Option<&[u8]> {
    let w = &st.windows[st.front_index()?];
    Some(&w.title[..w.title.iter().position(|b| *b == 0).unwrap_or(w.title.len())])
}
pub fn start(st: &mut GuiState, id: &[u8], op: u8, n: u16) -> Result<(), u8> {
    if st.ops.active || wm::gf_accept::busy() { return Err(4); }
    if usize::from(op) >= NAMES.len() || n == 0 || usize::from(n) > MAX || id.len() != 8 {
        return Err(3);
    }
    if (op == 0 && st.front_index().is_none()) || crate::fullscreen::active()
        || crate::modal::is_open() || (op == 2 && crate::startmenu::is_open())
        || gserial::queued(st) || st.pending_raw_n != 0 { return Err(3); }
    if op == 1 || op == 4 || op == 5 {
        let t = title(st);
        let known = matches!(t, Some(b"Edit") | Some(b"File Manager"));
        if !known {
            emit(format_args!("GOP1 {} SKIP title\r\n", NAMES[op as usize]));
            gserial::reply(id, format_args!("end 0")); return Ok(());
        }
        if t != Some(if op == 5 { b"File Manager" } else { b"Edit" }) { return Err(3); }
    }
    st.ops = Run::NEW;
    st.ops.op = op; st.ops.left = n; st.ops.id.copy_from_slice(id); st.ops.active = true;
    if let Some(i) = st.front_index() {
        let w = &st.windows[i];
        st.ops.origin = (w.x, w.y); st.ops.window = w.id(i); st.ops.owner = w.owner;
    }
    wm::gf_accept::pause(true);
    Ok(())
}
fn inject(st: &mut GuiState) -> bool {
    match st.ops.op {
        0 => {
            let x = st.ops.origin.0 + if st.ops.phase == 0 { 64 } else { 0 };
            wm::move_window(st, st.ops.owner, st.ops.window, x, st.ops.origin.1) == 0
        }
        3 => { wm::composite_full(st); true }
        op => {
            let key: &[u8] = match op {
                1 => b"37", // KEY_ROLLDOWN / PageDown
                2 => b"00+c", // CTRL+ESC: kbdnav::StartMenu
                4 => if (u16::from(st.ops.warm) + st.ops.i) & 1 == 0 { b"1d" } else { b"0e" },
                _ => b"3d", // KEY_DOWN
            };
            gserial::enqueue(st, key, false).is_ok()
        }
    }
}
fn drained(st: &GuiState, committed: bool) -> bool {
    st.windows.iter().all(|w| !w.used ||
        (w.dirty.len == 0 && w.issued.len == 0 && !w.configure_pending))
        && !gserial::queued(st) && st.pending_raw_n == 0
        && st.slots.iter().enumerate().all(|(i, s)| !s.used || ring::pending(st, i) == 0)
        && committed
}
fn end(st: &mut GuiState) {
    let r = &mut st.ops;
    r.active = false;
    r.samples[..r.count].sort_unstable();
    if r.count != 0 {
        let p50 = r.samples[(r.count * 50).div_ceil(100)-1];
        let p95 = r.samples[(r.count * 95).div_ceil(100)-1];
        emit(format_args!("GOP1 {} p50 {} p95 {} valid {} failed {}\r\n",
            NAMES[r.op as usize], p50, p95, r.count, usize::from(r.i)-r.count));
    } else {
        emit(format_args!("GOP1 {} p50 FAILED p95 FAILED valid 0 failed {}\r\n", NAMES[r.op as usize], r.i));
    }
    gserial::reply(&r.id, format_args!("end 0"));
    wm::gf_accept::pause(false);
}
/// Called only at the end of X3/standalone cycles, after flush_present.
pub fn on_cycle_end(st: &mut GuiState) {
    if !st.ops.active { return; }
    if !st.ops.waiting {
        st.ops.base = stats(); st.ops.last_commit = st.ops.base.commits;
        let t = now(); st.ops.valid = t.is_some(); st.ops.t0 = t.unwrap_or(0);
        st.ops.deadline = unsafe { (os32api::api().get_tick)() }.wrapping_add(TIMEOUT_TICKS);
        st.ops.waiting = true; st.ops.phase = 0;
        if !inject(st) { st.ops.valid = false; }
        return;
    }
    let s = stats();
    let done = drained(st, s.commits != st.ops.last_commit);
    st.ops.last_commit = s.commits;
    let tick = unsafe { (os32api::api().get_tick)() };
    let expired = (tick.wrapping_sub(st.ops.deadline) as i32) >= 0;
    if !done && !expired { return; }
    if done && !expired && st.ops.phase == 0 && matches!(st.ops.op, 0 | 2) {
        st.ops.phase = 1;
        if !inject(st) { st.ops.valid = false; }
        return;
    }
    let t = now();
    let r = &mut st.ops;
    let us = t.and_then(|t| t.checked_sub(r.t0)).filter(|v| *v < u64::from(FAILED));
    let us = if r.valid && !expired { us.map(|v| v as u32) } else { None };
    let pb = s.present_bytes.wrapping_sub(r.base.present_bytes);
    let cm = s.commits.wrapping_sub(r.base.commits);
    let hw = s.hw_ops.wrapping_sub(r.base.hw_ops);
    // Warmups use the same completion/deadline path, but never enter percentiles.
    if r.warm < WARM {
        if us.is_none() {
            emit(format_args!("GOP1 {} warm {} FAILED {} {} {}\r\n", NAMES[r.op as usize], r.warm, pb, cm, hw));
        }
        r.warm += 1;
    }
    else {
        match us {
            Some(us) => {
                r.samples[r.count] = us; r.count += 1;
                emit(format_args!("GOP1 {} {} {} {} {} {}\r\n", NAMES[r.op as usize], r.i, us, pb, cm, hw));
            }
            None => emit(format_args!("GOP1 {} {} FAILED {} {} {}\r\n", NAMES[r.op as usize], r.i, pb, cm, hw)),
        }
        r.i += 1; r.left -= 1;
    }
    r.waiting = false;
    if r.left == 0 { end(st); }
}

#[cfg(test)]
#[path = "../host/typical_ops_tests.rs"]
mod tests;
