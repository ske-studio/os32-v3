//! GSERIAL v1: SOH resynchronization, atomic key queue and X3 delivery.
use core::fmt::{self, Write};
use crate::{input, modal, ring, session, startmenu, wm::GuiState};
use os32api::gui::proto::GUI_SESSION_SWITCH_CUI;
use os32api::gui::types::{ScreenInfo, Stats, GFX_FMT_PACKED8, GFX_CAP_TEXT_OVERLAY};
include!(concat!(env!("OUT_DIR"), "/diag_points.rs"));
include!(concat!(env!("OUT_DIR"), "/remote_control.rs"));
const LINE_MAX: usize = REMOTE_GSERIAL_LINE_MAX as usize;
const KEY_MAX: usize = 128;
const RAW_MAX: usize = 512;
const TICKS_SECOND: u32 = 100; // memmap.h PIT 10ms
pub(crate) const TIMEOUT_MAX: u32 = i32::MAX as u32 / TICKS_SECOND;
const MOD_GRPH: u32 = 8;
const MOD_KANA: u32 = 4;
const SC_GRPH: u8 = 0x73; // kbd.h KEY_GRPH
const SC_KANA: u8 = 0x72;

pub struct Serial {
    enabled: bool,
    lan_enabled: bool,
    lan: bool,
    synced: bool,
    line: [u8; LINE_MAX],
    len: usize,
    junk: bool,
    long: bool,
    cr: bool,
    raw: [u16; RAW_MAX],
    lengths: [u8; KEY_MAX],
    events: [u8; KEY_MAX],
    keys: usize,
    raws: usize,
    sent: u32,
    seconds: u32,
    deadline: u32,
    expired: bool,
}
impl Serial {
    pub const NEW: Self = Self {
        enabled: false, lan_enabled: false, lan: false, synced: false, line: [0; LINE_MAX], len: 0,
        junk: false, long: false, cr: false, raw: [0; RAW_MAX],
        lengths: [0; KEY_MAX], events: [0; KEY_MAX], keys: 0, raws: 0,
        sent: 0, seconds: 0, deadline: 0, expired: false,
    };
    fn arm(&mut self, now: u32, seconds: u32) {
        self.seconds = seconds;
        self.deadline = now.wrapping_add(seconds * TICKS_SECOND);
        self.expired = false;
    }
    fn remaining(&self, now: u32) -> u32 {
        if self.seconds == 0 || self.expired { 0 }
        else { self.deadline.wrapping_sub(now).div_ceil(TICKS_SECOND) }
    }
}

/// One NUL-terminated wire line, bounded to 200 bytes including CRLF.
struct Line { bytes: [u8; 201], n: usize }
impl Write for Line {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        if self.n + s.len() > 198 { return Err(fmt::Error); }
        self.bytes[self.n..self.n+s.len()].copy_from_slice(s.as_bytes());
        self.n += s.len();
        Ok(())
    }
}
// Only the cooperative foreground owns these; callbacks never poll recursively.
static mut LINK: Option<crate::gserial_link::Link> = None;
static mut RESPONSE_LAN: bool = false;
pub(crate) fn response_lan() -> bool { unsafe { RESPONSE_LAN } }
pub(crate) fn reply(id: &[u8], args: fmt::Arguments<'_>) {
    reply_to(response_lan(), id, args);
}
pub(crate) fn reply_to(lan: bool, id: &[u8], args: fmt::Arguments<'_>) {
    let mut line = Line { bytes: [0; 201], n: 0 };
    let wire_id = id;
    let id = core::str::from_utf8(id).unwrap_or("-");
    let _ = write!(line, "gs:{} {}", id, args);
    line.bytes[line.n..line.n+2].copy_from_slice(b"\r\n");
    if lan {
        unsafe {
            if let Some(link) = (*core::ptr::addr_of_mut!(LINK)).as_mut() {
                link.emit(wire_id, &line.bytes[..line.n+2], (os32api::api().get_tick)().wrapping_mul(10));
            }
        }
    } else { unsafe { (os32api::api().serial_puts)(line.bytes.as_ptr()); } }
}
pub fn notify(st: &GuiState, message: &str) {
    if st.gserial.enabled || st.gserial.lan_enabled {
        reply_to(st.gserial.lan, b"!", format_args!("{}", message));
    }
}
pub fn disarm(st: &mut GuiState) { st.gserial.arm(st.now, 0); }
pub fn lost(st: &mut GuiState) { st.inj_lost = st.inj_lost.wrapping_add(1); }
pub fn cui_done(st: &GuiState) { notify(st, "cui done"); }
fn end(id: &[u8], rc: u8) { reply(id, format_args!("end {}", rc)); }
fn error(id: &[u8], message: &str, rc: u8) {
    reply(id, format_args!("err {}", message)); end(id, rc);
}

pub fn open(st: &mut GuiState, seconds: u32) {
    st.gserial = Serial::NEW;
    unsafe { LINK = None; RESPONSE_LAN = false; }
    if unsafe { (os32api::api().serial_is_initialized)() } == 0 { return; }
    unsafe { (os32api::api().rshell_set_active)(0); }
    st.gserial.enabled = true;
    st.gserial.arm(unsafe { (os32api::api().get_tick)() }, seconds.min(TIMEOUT_MAX));
    reply_to(false, b"!", format_args!("ready v1"));
}
/// Enable the LAN independently of UART initialization (G3 calls this at startup).
pub fn open_lan(st: &mut GuiState, seconds: u32) {
    unsafe { LINK = Some(crate::gserial_link::Link::new(st.now.wrapping_mul(10))); }
    st.gserial.lan_enabled = true;
    st.gserial.lan = true;
    st.gserial.arm(st.now, seconds.min(TIMEOUT_MAX));
    reply_to(true, b"!", format_args!("ready v1"));
}
/// Advance G1, then release its borrow before executing a received command.
pub fn poll_lan(st: &mut GuiState) {
    if !st.gserial.lan_enabled { return; }
    let mut bytes = [0; LINE_MAX + 2]; let mut len = 0;
    unsafe {
        if let Some(link) = (*core::ptr::addr_of_mut!(LINK)).as_mut() {
            link.poll(st.now.wrapping_mul(10), |line| {
                len = line.len(); bytes[..len].copy_from_slice(line);
            });
        }
    }
    if len != 0 { receive_line(st, &bytes[..len]); }
}
pub fn shutdown_pending(st: &GuiState) -> bool {
    unsafe { (*core::ptr::addr_of_mut!(LINK)).as_mut()
        .is_some_and(|link| link.shutdown_pending(st.now.wrapping_mul(10))) }
}
fn decimal(s: &[u8]) -> Option<u32> {
    if s.is_empty() { return None; }
    let mut n = 0u32;
    for b in s {
        if !b.is_ascii_digit() { return None; }
        n = n.checked_mul(10)?.checked_add((b-b'0') as u32)?;
    }
    (n <= TIMEOUT_MAX).then_some(n)
}
pub fn load_timeout() -> u32 {
    let mut bytes = [0; crate::CFG_BUF];
    let a = unsafe { os32api::api() };
    let fd = unsafe { (a.sys_open)(crate::SYSTEM_CFG.as_ptr(), 0) };
    if fd < 0 { return 0; }
    let mut n = 0;
    let mut failed = false;
    while n < bytes.len() {
        let r = unsafe { (a.sys_read)(fd, bytes[n..].as_mut_ptr(), (bytes.len()-n) as u32) };
        if r < 0 { failed = true; break; }
        if r == 0 { break; }
        n += r as usize;
    }
    unsafe { (a.sys_close)(fd); }
    if failed || n == bytes.len() { return 0; }
    for line in bytes[..n].split(|b| *b == b'\n') {
        if let Some(equal) = line.iter().position(|b| *b == b'=') {
            if line[..equal].trim_ascii() == b"GSERIAL_TIMEOUT" {
                return decimal(line[equal+1..].trim_ascii()).unwrap_or(0);
            }
        }
    }
    0
}

#[derive(Debug, PartialEq)]
pub enum Cmd<'a> {
    Ping, State, Px, Dump, Key(&'a [u8]), Type(&'a [u8]), Cui, Wd(u32), Flush,
    #[cfg(feature = "gf_accel_accept")]
    Ops(u8, u16),
    Invalid, Unknown,
}
pub fn parse(body: &[u8]) -> Cmd<'_> {
    match body {
        b"ping" => Cmd::Ping, b"state" => Cmd::State, b"dump" => Cmd::Dump,
        b"px" => Cmd::Px, b"cui" => Cmd::Cui, b"flush" => Cmd::Flush,
        _ => {
            let cut = body.iter().position(|b| *b == b' ').unwrap_or(body.len());
            let (verb, rest) = body.split_at(cut);
            let arg = rest.get(1..).unwrap_or(b"");
            match verb {
                b"key" if !arg.is_empty() => Cmd::Key(arg),
                b"type" if !arg.is_empty() => Cmd::Type(arg),
                #[cfg(feature = "gf_accel_accept")]
                b"ops" => crate::typical_ops::parse(arg).map_or(Cmd::Invalid, |(op, n)| Cmd::Ops(op, n)),
                b"wd" => decimal(arg).map_or(Cmd::Invalid, Cmd::Wd),
                b"ping" | b"state" | b"px" | b"dump" | b"cui" | b"flush" | b"key" | b"type" | b"" => Cmd::Invalid,
                _ => Cmd::Unknown,
            }
        }
    }
}
fn hex(b: u8) -> Option<u8> {
    match b { b'0'..=b'9' => Some(b-b'0'), b'a'..=b'f' => Some(b-b'a'+10),
        b'A'..=b'F' => Some(b-b'A'+10), _ => None }
}
fn key(token: &[u8]) -> Option<(u8, u32)> {
    if token.len() < 2 { return None; }
    let scan = hex(token[0])? * 16 + hex(token[1])?;
    if scan >= 128 { return None; }
    let mut mods = 0;
    for part in token[2..].chunks(2) {
        if part.len() != 2 || part[0] != b'+' { return None; }
        let bit = match part[1] { b's' => 1, b'c' => 16, b'g' => MOD_GRPH, b'k' => MOD_KANA, _ => return None };
        if mods & bit != 0 { return None; }
        mods |= bit;
    }
    Some((scan, mods))
}
fn raw(scan: u8, down: bool, mods: u32) -> u16 {
    scan as u16 | (u16::from(down) << 8) | ((mods as u16) << 9)
}
fn expand(scan: u8, mods: u32) -> ([u16; 6], usize, u8) {
    let mut out = [0; 6]; let mut n = 0;
    for (sc, bit) in [(SC_GRPH, MOD_GRPH), (SC_KANA, MOD_KANA)] {
        if mods & bit != 0 { out[n] = raw(sc, true, mods); n += 1; }
    }
    out[n] = raw(scan, true, mods); n += 1;
    out[n] = raw(scan, false, mods); n += 1;
    let mut released = mods;
    for (sc, bit) in [(SC_KANA, MOD_KANA), (SC_GRPH, MOD_GRPH)] {
        if mods & bit != 0 { released &= !bit; out[n] = raw(sc, false, released); n += 1; }
    }
    let ch = input::translate(scan, mods);
    let events = if (0x70..=0x74).contains(&scan) { 0 }
                 else if (0x20..=0x7e).contains(&ch) { 3 } else { 2 };
    (out, n, events)
}
pub(crate) fn enqueue(st: &mut GuiState, arg: &[u8], ascii: bool) -> Result<usize, &'static str> {
    // Parse and expand the whole line before changing the persistent queue.
    let mut raws = [0u16; RAW_MAX];
    let mut lengths = [0u8; KEY_MAX]; let mut events = [0u8; KEY_MAX];
    let mut nk = 0; let mut nr = 0;
    let mut add = |scan, mods| -> Result<(), &'static str> {
        let (r, n, ev) = expand(scan, mods);
        if nk == KEY_MAX || nr+n > RAW_MAX { return Err("full"); }
        raws[nr..nr+n].copy_from_slice(&r[..n]); lengths[nk] = n as u8; events[nk] = ev;
        nk += 1; nr += n; Ok(())
    };
    if ascii {
        for b in arg { let (s, m) = input::ascii_key(*b).ok_or("char")?; add(s,m)?; }
    } else {
        for token in arg.split(|b| *b == b' ').filter(|s| !s.is_empty()) {
            let (s,m) = key(token).ok_or("args")?; add(s,m)?;
        }
        if nk == 0 { return Err("args"); }
    }
    let q = &mut st.gserial;
    if q.keys+nk > KEY_MAX || q.raws+nr > RAW_MAX { return Err("full"); }
    q.raw[q.raws..q.raws+nr].copy_from_slice(&raws[..nr]);
    q.lengths[q.keys..q.keys+nk].copy_from_slice(&lengths[..nk]);
    q.events[q.keys..q.keys+nk].copy_from_slice(&events[..nk]);
    q.keys += nk; q.raws += nr;
    Ok(nk)
}
pub fn deliver(st: &mut GuiState) {
    while st.gserial.keys > 0 {
        let n_raw = st.gserial.lengths[0] as usize;
        let n_ev = st.gserial.events[0] as usize;
        if st.pending_raw.len() - st.pending_raw_n < n_raw { break; }
        if !modal::is_open() && !startmenu::is_open() {
            if let Some(t) = input::focus_target(st) {
                if usize::from(ring::space(st, t.slot)) < 4 + 2 * st.pending_raw_n + n_ev { break; }
            }
        }
        for i in 0..n_raw { st.pending_raw[st.pending_raw_n+i] = st.gserial.raw[i] as i32; }
        st.pending_raw_n += n_raw;
        let q = &mut st.gserial;
        q.raw.copy_within(n_raw..q.raws, 0); q.raws -= n_raw;
        q.lengths.copy_within(1..q.keys, 0); q.events.copy_within(1..q.keys, 0);
        q.keys -= 1; q.sent = q.sent.wrapping_add(1);
    }
}
fn execute(st: &mut GuiState, id: &[u8], body: &[u8]) {
    match parse(body) {
        Cmd::Ping => reply(id, format_args!("pong tick={}", st.now)),
        Cmd::State => reply(id, format_args!(
            "state tick={} win={} front={}/{} modal={} menu={} fs={} fep={} act={} mouse={},{} q={} sent={} lost={} drop={} wd={}",
            st.now, st.windows.iter().filter(|w| w.used).count(), st.front_owner(), st.front_id(),
            u8::from(modal::is_open()), u8::from(startmenu::is_open()), crate::fullscreen::owner(),
            u8::from(crate::fep::is_on()), session::pending_action(), st.mouse_x, st.mouse_y,
            st.gserial.keys, st.gserial.sent, st.inj_lost,
            unsafe { (os32api::api().kbd_dropped_count)() }, st.gserial.remaining(st.now))),
        Cmd::Px => unsafe {
            let api = os32api::api();
            let mut info = ScreenInfo::ZERO;
            (api.gfx_screen_info)(&mut info as *mut ScreenInfo as *mut u8);
            // Existing backend capabilities distinguish PEGC from Cirrus/planar.
            if info.height != 480 || info.bpp != 8 || info.format != GFX_FMT_PACKED8 ||
                info.flags != GFX_CAP_TEXT_OVERLAY || crate::ffi::gfx_ready == 0 {
                reply(id, format_args!("px na"));
            } else {
                let mut stats = Stats::ZERO;
                (api.gfx_stats)(&mut stats as *mut Stats as *mut u8);
                reply(id, format_args!("px {:02x},{:02x} pb={} cm={}",
                    crate::ffi::gfx_get_pixel(OS32_GUI_DIAG_P1_X, OS32_GUI_DIAG_P1_Y),
                    crate::ffi::gfx_get_pixel(OS32_GUI_DIAG_P2_X, OS32_GUI_DIAG_P2_Y),
                    stats.present_bytes, stats.commits));
            }
        },
        Cmd::Dump => crate::diagnostics::dump_with(|args| reply(id, args)),
        Cmd::Cui => {
            if session::set_wm(st, GUI_SESSION_SWITCH_CUI, b"") != 0 { error(id, "busy", 4); return; }
            reply(id, format_args!("cui queued"));
        }
        Cmd::Wd(seconds) => { st.gserial.arm(st.now, seconds); reply(id, format_args!("wd {}", seconds)); }
        Cmd::Flush => {
            let n = st.gserial.keys; st.gserial.keys = 0; st.gserial.raws = 0;
            reply(id, format_args!("flush n={}", n));
        }
        Cmd::Key(arg) | Cmd::Type(arg) => {
            let ascii = matches!(parse(body), Cmd::Type(_));
            match enqueue(st, arg, ascii) {
                Ok(n) => reply(id, format_args!("{} queued n={}", if ascii { "type" } else { "key" }, n)),
                Err(msg) => { error(id, msg, if msg == "full" {4} else {1}); return; }
            }
        }
        #[cfg(feature = "gf_accel_accept")]
        Cmd::Ops(op, n) => {
            if let Err(rc) = crate::typical_ops::start(st, id, op, n) {
                error(id, if rc == 4 { "busy" } else { "pre" }, rc);
            }
            return;
        }
        Cmd::Invalid => { error(id, "args", 1); return; }
        Cmd::Unknown => { error(id, "unknown", 2); return; }
    }
    end(id, 0);
}
/// Complete LAN line; its framing state must never consume a UART fragment.
pub fn receive_line(st: &mut GuiState, bytes: &[u8]) {
    let q = &mut st.gserial;
    let saved = (q.synced, q.line, q.len, q.junk, q.long, q.cr);
    q.synced = false;
    for &b in bytes { if receive_byte(st, b, true) { break; } }
    let q = &mut st.gserial;
    (q.synced, q.line, q.len, q.junk, q.long, q.cr) = saved;
}
fn receive(st: &mut GuiState) {
    loop {
        let b = unsafe { (os32api::api().serial_trygetchar)() };
        if b < 0 { return; }
        if receive_byte(st, b as u8, false) { return; }
    }
}
fn receive_byte(st: &mut GuiState, b: u8, lan: bool) -> bool {
    let q = &mut st.gserial;
    if b == 1 {
        q.synced = true; q.len = 0; q.junk = false; q.long = false; q.cr = false;
        return false;
    }
    if b == b'\n' {
        if !q.synced { return true; }
        q.synced = false;
        let mut line = [0; LINE_MAX]; let len = q.len;
        line[..len].copy_from_slice(&q.line[..len]);
        let junk = q.junk; let long = q.long;
        if q.seconds != 0 { q.arm(st.now, q.seconds); }
        unsafe { RESPONSE_LAN = lan; }
        q.lan = lan;
        let id_ok = len >= 9 && line[..8].iter().all(|b| hex(*b).is_some()) && line[8] == b' ';
        let id: &[u8] = if id_ok { &line[..8] } else { b"-" };
        if junk || !id_ok { error(id, "junk", 3); }
        else if long { error(id, "long", 3); }
        else { execute(st, id, &line[9..len]); }
        return true;
    }
    if !q.synced { return false; }
    if q.cr { q.junk = true; }
    if b == b'\r' { q.cr = true; return false; }
    if !(0x20..=0x7e).contains(&b) { q.junk = true; }
    if q.len == LINE_MAX { q.long = true; }
    else { q.line[q.len] = b; q.len += 1; }
    false
}
pub fn poll_x3(st: &mut GuiState) {
    if !st.gserial.enabled && !st.gserial.lan_enabled { return; }
    st.now = unsafe { (os32api::api().get_tick)() };
    cycle(st);
}
pub fn cycle(st: &mut GuiState) {
    if !st.gserial.enabled && !st.gserial.lan_enabled { return; }
    if st.gserial.enabled { receive(st); }
    if st.gserial.seconds != 0 && (st.now.wrapping_sub(st.gserial.deadline) as i32) >= 0 {
        if !st.gserial.expired { notify(st, "wd expired"); st.gserial.expired = true; }
        let _ = session::set_wm(st, GUI_SESSION_SWITCH_CUI, b"");
    }
    deliver(st);
}

#[cfg(test)]
#[path = "../host/gserial_tests.rs"]
mod tests;

#[cfg(feature = "gf_accel_accept")]
pub(crate) fn queued(st: &GuiState) -> bool { st.gserial.keys != 0 }

#[cfg(test)]
#[path = "../host/remote_g2_tests.rs"]
mod remote_tests;
