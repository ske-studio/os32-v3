//! G1 LAN transport. G2 supplies receive_line/emit, G3 owns polling and shutdown.
//! All times are wrapping milliseconds; poll never waits or touches the UART.
use core::fmt::{self, Write};
include!(concat!(env!("OUT_DIR"), "/remote_control.rs"));
const WIRE_MAX: usize = REMOTE_GSERIAL_LINE_MAX as usize + 2; // SOH and LF
const FIFO: usize = 8192; // LAN_OPERATOR_DESIGN §4

struct Line { bytes: [u8; REMOTE_GSERIAL_LINE_MAX as usize], n: usize }
impl Write for Line {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        if self.n + s.len() > self.bytes.len() { return Err(fmt::Error); }
        self.bytes[self.n..self.n+s.len()].copy_from_slice(s.as_bytes());
        self.n += s.len(); Ok(())
    }
}
#[repr(C)]
struct Notice {
    serial: u32, dropped: u32, generation: u32,
    service: u16, kind: u16, length: u16, reserved: u16, data: [u8; LINK_NOTIFY_DATA_MAX as usize],
}

pub struct Link {
    boot: [u8; 32], generation: u32, ready: bool, retry: u32,
    last: Option<u32>, id: [u8; REMOTE_ID8_HEX_DIGITS as usize], done: bool,
    body: [u8; FIFO], len: usize, frozen: bool, drop: u32,
    notice: [u8; LINK_MAX_PAYLOAD as usize], notice_len: usize, notice_drop: u32,
    h: i32, opening_ready: bool, sending_notice: bool, sent: usize,
    response: [u8; 64], received: usize,
    started: u32, progress: u32, queued: u32,
}
impl Link {
    /// Call after os32api initialization. Boot identity survives gshell restarts.
    pub fn new(now: u32) -> Self {
        let mut token = [0u8; 16];
        unsafe { (os32api::api().boot_token_get)(token.as_mut_ptr()); }
        let mut boot = [0; 32];
        for (i, b) in token.iter().enumerate() {
            boot[2*i] = b"0123456789abcdef"[(b >> 4) as usize];
            boot[2*i+1] = b"0123456789abcdef"[(b & 15) as usize];
        }
        Self { boot, generation: 0, ready: false, retry: now, last: None, id: [0;REMOTE_ID8_HEX_DIGITS as usize],
            done: false, body: [0;FIFO], len: 0, frozen: false, drop: 0,
            notice: [0;LINK_MAX_PAYLOAD as usize], notice_len: 0, notice_drop: 0,
            h: -1, opening_ready: false, sending_notice: false, sent: 0,
            response: [0;64], received: 0, started: 0, progress: 0, queued: 0 }
    }
    /// A complete gs:! record occupies the independent asynchronous slot.
    /// Command output may arrive in fragments; only its final end N freezes it.
    pub fn emit(&mut self, id: &[u8], bytes: &[u8], now: u32) {
        if id == b"!" {
            if self.notice_len != 0 || bytes.len() > self.notice.len() || bytes.is_empty() {
                self.notice_drop = self.notice_drop.saturating_add(bytes.len() as u32);
                if self.h >= 0 && !self.opening_ready && self.sending_notice {
                    self.close(); self.notice_len = 0;
                }
                return;
            }
            self.notice[..bytes.len()].copy_from_slice(bytes);
            self.notice_len = bytes.len(); self.queued = now; return;
        }
        if self.last.is_none() || id != self.id || self.frozen || self.done { return; }
        let n = bytes.len().min(FIFO - self.len);
        self.body[self.len..self.len+n].copy_from_slice(&bytes[..n]); self.len += n;
        self.drop = self.drop.saturating_add((bytes.len()-n) as u32);
        if has_end(&self.body[..self.len], &self.id) { self.frozen = true; }
    }
    pub fn pending(&self) -> bool { self.frozen || self.notice_len != 0 || self.h >= 0 }
    /// Cooperative GUI-release gate: caller keeps WM polling until false.
    /// Timeouts discard the output so the host retains UNKNOWN.
    pub fn shutdown_pending(&mut self, now: u32) -> bool {
        if self.notice_len != 0 && now.wrapping_sub(self.queued) >= 5000 {
            self.close(); self.notice_len = 0; self.frozen = false; return false;
        }
        self.pending()
    }
    fn close(&mut self) {
        if self.h >= 0 { unsafe { (os32api::api().host_close)(self.h); } }
        self.h = -1;
    }
    fn failed(&mut self, now: u32) {
        self.close(); self.ready = false; self.retry = now.wrapping_add(2000);
        // Preserve last-ID to prevent re-execution, but never replay partial OUT.
        if !self.opening_ready {
            if self.sending_notice { self.notice_len = 0; self.notice_drop = 0; }
            else { self.frozen = false; self.len = 0; self.done = true; }
        }
    }
    /// At most one notification and one WDATA chunk per foreground cycle.
    /// Returned bytes go unchanged to the G2 shared GSERIAL receive_line parser.
    pub fn poll(&mut self, now: u32, mut receive_line: impl FnMut(&[u8])) {
        if self.h >= 0 {
            if now.wrapping_sub(self.progress) >= 1000 || now.wrapping_sub(self.started) >= 5000 {
                self.failed(now); return;
            }
            self.advance(now); return;
        }
        if !self.ready {
            if now.wrapping_sub(self.retry) >= 0x8000_0000 { return; }
            let mut line = Line { bytes: [0;REMOTE_GSERIAL_LINE_MAX as usize], n: 0 };
            let boot = core::str::from_utf8(&self.boot).unwrap();
            let _ = write!(line, "RSH READY {} gui 1 ", boot);
            if self.last.is_some() {
                let _ = write!(line, "{} {}", core::str::from_utf8(&self.id).unwrap(), if self.done {"done"} else {"running"});
            } else { let _ = line.write_str("none idle"); }
            self.opening_ready = true; self.open(&line, now); return;
        }
        if self.frozen || self.notice_len != 0 {
            self.sending_notice = !self.frozen;
            let (id, drop, len) = if self.sending_notice {
                (b"!".as_slice(), self.notice_drop, self.notice_len)
            } else { (self.id.as_slice(), self.drop, self.len) };
            let mut line = Line { bytes: [0;REMOTE_GSERIAL_LINE_MAX as usize], n: 0 };
            let _ = write!(line, "GS OUT {} {} {} {}", core::str::from_utf8(&self.boot).unwrap(),
                core::str::from_utf8(id).unwrap(), drop, len);
            self.opening_ready = false; self.open(&line, now); return;
        }
        let mut n = Notice { serial: 0, dropped: 0, generation: 0, service: 0,
            kind: 0, length: 0, reserved: 0, data: [0;LINK_NOTIFY_DATA_MAX as usize] };
        let rc = unsafe { (os32api::api().host_notice_read)(self.generation,
            REMOTE_SVC_GSERIAL as u32, &mut n as *mut _ as *mut u8) };
        if rc == OS32_ERR_STALE { self.ready = false; self.retry = now.wrapping_add(2000); return; }
        if rc == OS32_ERR_AGAIN && now.wrapping_sub(self.retry) < 0x8000_0000 {
            self.ready = false; self.retry = now; return;
        }
        if rc != 0 || n.generation != self.generation || n.service != REMOTE_SVC_GSERIAL as u16 { return; }
        if n.kind == REMOTE_NOTIFY_PROBE as u16 { self.ready = false; self.retry = now; return; }
        let len = n.length as usize;
        if n.kind != REMOTE_NOTIFY_EXECUTE as u16 || len > WIRE_MAX || len < 11 { return; }
        let data = &n.data[..len];
        if data[0] != 1 || data[9] != b' ' || data[len-1] != b'\n' { return; }
        let id = match hex(&data[1..9]) { Some(id) => id, None => return };
        if self.last.map_or(false, |last| id <= last || !self.done) { return; }
        self.last = Some(id); self.id.copy_from_slice(&data[1..9]);
        self.done = false; self.len = 0; self.drop = 0;
        receive_line(data);
    }
    fn open(&mut self, line: &Line, now: u32) {
        self.h = unsafe { (os32api::api().host_open)(line.bytes.as_ptr(), line.n as u32) };
        self.sent = 0; self.received = 0; self.started = now; self.progress = now;
        if self.h < 0 { self.ready = false; self.retry = now.wrapping_add(2000); }
    }
    fn advance(&mut self, now: u32) {
        let api = unsafe { os32api::api() };
        if !self.opening_ready {
            let body = if self.sending_notice { &self.notice[..self.notice_len] } else { &self.body[..self.len] };
            if self.sent < body.len() {
                let bytes = &body[self.sent..];
                let rc = unsafe { (api.host_write)(self.h, bytes.as_ptr(), bytes.len().min(LINK_MAX_PAYLOAD as usize) as u32) };
                if rc > 0 && rc as usize <= bytes.len().min(LINK_MAX_PAYLOAD as usize) {
                    self.sent += rc as usize; self.progress = now;
                } else if rc != OS32_ERR_AGAIN && rc != OS32_ERR_FULL { self.failed(now); }
                return;
            }
        }
        let (mut status, mut length) = (0, 0);
        let rc = unsafe { (api.host_status)(self.h, &mut status, &mut length) };
        if rc == OS32_ERR_AGAIN { return; }
        if rc != 0 || status != 200 { self.failed(now); return; }
        if self.opening_ready {
            if length as usize > self.response.len() { self.failed(now); return; }
            if self.received < length as usize {
                let cap = length as usize - self.received;
                let rc = unsafe { (api.host_read)(self.h, self.response[self.received..].as_mut_ptr(), cap as u32) };
                if rc == OS32_ERR_AGAIN { return; }
                if rc <= 0 || rc as usize > cap { self.failed(now); return; }
                self.received += rc as usize; self.progress = now;
                if self.received < length as usize { return; }
            }
            let parsed = ready_body(&self.response[..self.received]);
            if let Some(generation) = parsed {
                if generation != self.generation {
                    let reset_ready = self.last.is_some();
                    self.last = None; self.done = false; self.frozen = false;
                    self.len = 0;
                    // Keep the startup gs:! ready queued before the first READY.
                    if self.generation != 0 { self.notice_len = 0; self.notice_drop = 0; }
                    if reset_ready {
                        self.generation = generation; self.ready = false; self.retry = now;
                        self.close(); return;
                    }
                }
                self.generation = generation; self.ready = true;
                self.retry = now.wrapping_add(2000);
            } else { self.failed(now); return; }
        } else if self.sending_notice { self.notice_len = 0; self.notice_drop = 0; }
        else { self.frozen = false; self.len = 0; self.done = true; }
        self.close();
    }
}
fn hex(bytes: &[u8]) -> Option<u32> {
    if bytes.len() != REMOTE_ID8_HEX_DIGITS as usize { return None; }
    bytes.iter().try_fold(0u32, |n, b| {
        let d = (*b as char).to_digit(16)?; Some((n << 4) | d)
    })
}
fn ready_body(bytes: &[u8]) -> Option<u32> {
    let text = core::str::from_utf8(bytes).ok()?.strip_suffix('\n')?;
    let (gen, svc) = text.strip_prefix("gen ")?.split_once(" svc ")?;
    if svc.parse::<u32>().ok()? != REMOTE_SVC_GSERIAL as u32 { return None; }
    gen.parse::<u32>().ok().filter(|g| *g != 0)
}
fn has_end(bytes: &[u8], id: &[u8]) -> bool {
    let line = bytes.strip_suffix(b"\n").unwrap_or(&[]);
    let line = line.rsplit(|b| *b == b'\n').next().unwrap_or(&[]);
    let line = line.strip_suffix(b"\r").unwrap_or(line);
    if !line.starts_with(b"gs:") || line.get(3..11) != Some(id) { return false; }
    let rc = match line.get(11..).and_then(|s| s.strip_prefix(b" end ")) { Some(rc) => rc, None => return false };
    !rc.is_empty() && rc.iter().all(u8::is_ascii_digit)
}
