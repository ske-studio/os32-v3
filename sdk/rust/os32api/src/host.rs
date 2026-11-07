//! Host Services: legacy C ABI and KAPI-backed Rust helpers.
//! `os32.` は OS32 標準、ske-studio が定めた。
//! New helpers use generated KAPI slots directly; legacy declarations remain
//! for libos32gui. Initialize os32api before calling these helpers.

#![allow(dead_code)]

use core::ffi::c_void;

/* ================================================================ */
/*  エラー (負値、libos32host.h の HOST_E*)                          */
/*                                                                  */
/*  正典は userland/lib/host/libos32host.h。ここは写し。            */
/* ================================================================ */

/// STALE / リンク未確立。
pub const HOST_ELINK: i32 = -100;
/// NIC 無し (KAPI が NOSYS)。
pub const HOST_ENODEV: i32 = -101;
/// 無進捗期限。
pub const HOST_ETIMEOUT: i32 = -102;
/// print/clip の 409/500/503 (業務失敗、`*svc_status` に業務値)。
pub const HOST_ESERVICE: i32 = -103;
/// 引数不正 (url 長・clip 長・空)。
pub const HOST_EINVAL: i32 = -104;
/// 予期しない負値。
pub const HOST_EIO: i32 = -105;
/// sink / src が中断を要求した。
pub const HOST_EABORT: i32 = -106;

/* ================================================================ */
/*  寸法 ([C4] の管理元は libos32host.h。ここは写し)                  */
/* ================================================================ */
/// host_open 要求行の上限。
pub const HOST_REQ_MAX: usize = 1400;
/// `"YYYY-MM-DD HH:MM:SS"` + NUL。
pub const HOST_TIME_BUF: usize = 20;
/// CLIP PUT の上限。
pub const HOST_CLIP_MAX: usize = 4096;

/* ================================================================ */
/*  ストリーミングのコールバック (libos32host.h と同型)              */
/* ================================================================ */

/// 受信本文を 1 チャンクずつ渡す。0 = 受理、非 0 = 中断 (EABORT)。
pub type HostSinkFn = extern "C" fn(ud: *mut c_void, buf: *const u8, n: u32) -> i32;
/// 送信本文を cap まで詰める。>0 = 長さ、0 = EOF、<0 = 中断 (EABORT)。
pub type HostSourceFn = extern "C" fn(ud: *mut c_void, buf: *mut u8, cap: u32) -> i32;

/* ================================================================ */
/*  libos32host.h の関数 (i386 cdecl)                               */
/*                                                                  */
/*  `int *http_status` は `*mut i32`、`u32 *pages/nbytes/svc_status` */
/*  は `*mut u32`。いずれも NULL 可 (C 側が見てから触る)。            */
/* ================================================================ */
extern "C" {
    /// `out` (20B) にホスト時刻を NUL 終端で書く。0 / 負。
    pub fn host_time(out: *mut u8) -> i32;

    /// `url` (NUL 終端) を GET。本文を `sink` にストリーミングし、`http_status`
    /// を sink の前に書く。`nbytes` = 受信実長。戻り 0 / 負 (`HOST_E*`)。
    pub fn host_get(
        url: *const u8,
        sink: HostSinkFn,
        ud: *mut c_void,
        http_status: *mut i32,
        nbytes: *mut u32,
    ) -> i32;

    /// `name` (NUL 終端) で `buf`/`len` を印刷。`pages` = 枚数、`svc_status` =
    /// 業務値 (409/500)。戻り 0 / 負。
    pub fn host_print_text(
        name: *const u8,
        buf: *const u8,
        len: u32,
        pages: *mut u32,
        svc_status: *mut u32,
    ) -> i32;

    /// `host_print_text` と同じだが本文を `src` から引く (メモリ一定)。
    pub fn host_print_stream(
        name: *const u8,
        src: HostSourceFn,
        ud: *mut c_void,
        pages: *mut u32,
        svc_status: *mut u32,
    ) -> i32;

    /// ホストのクリップボードを取得し `sink` にストリーミング。`nbytes` =
    /// 受信実長、`svc_status` = 業務値 (503)。戻り 0 / 負。
    pub fn host_clip_get(
        sink: HostSinkFn,
        ud: *mut c_void,
        nbytes: *mut u32,
        svc_status: *mut u32,
    ) -> i32;

    /// `buf`/`len` (1〜4096) をホストのクリップボードへ。`svc_status` = 業務値。
    pub fn host_clip_put(buf: *const u8, len: u32, svc_status: *mut u32) -> i32;
}

use alloc::{format, vec, vec::Vec};
use core::cell::UnsafeCell;
// Mirrors os32_kapi_shared.h, as ls.rs does. No SDK-private error numbers.
const IO: i32 = -1;
const NOTFOUND: i32 = -2;
const NOSPC: i32 = -4;
const INVAL: i32 = -9;
const STALE: i32 = -11;
const FULL: i32 = -13;
const AGAIN: i32 = -14;
const HOSTDOWN: i32 = -18;
const TIMEOUT: i32 = -19;
const DECL_MAX: usize = 65536;
const WRITE_CHUNK: usize = 1400;

struct Deadline { start: u32, progress: u32, ticks: u32, nonblocking: bool }
impl Deadline {
    unsafe fn new(ms: u32) -> Self {
        let now = (crate::api().get_tick)();
        Self { start: now, progress: now, ticks: ms / 10 + u32::from(ms % 10 != 0), nonblocking: ms == 0 }
    }
    unsafe fn expired(&self) -> i32 {
        let now = (crate::api().get_tick)();
        if !self.nonblocking && now.wrapping_sub(self.start) >= self.ticks { TIMEOUT }
        else if now.wrapping_sub(self.progress) >= 100 { HOSTDOWN }
        else { 0 }
    }
    unsafe fn wait(&self, rc: i32) -> i32 {
        if self.nonblocking { return rc; }
        let error = self.expired();
        if error != 0 { return error; }
        (crate::api().sys_yield)();
        0
    }
}

/// Execute a request; success is the body byte count. `timeout` is the whole
/// call's deadline in milliseconds; zero never yields. Business status belongs
/// to the service protocol. Insufficient output returns NOSPC, never truncation.
/// Input length must match the declaration in the request line.
pub fn host_call(req: &str, input: &[u8], output: &mut [u8], timeout: u32) -> i32 {
    unsafe {
        let rc = call(req, input, output, &mut Deadline::new(timeout));
        if rc == STALE { *REGISTRY.0.get() = None; }
        rc
    }
}

unsafe fn call(req: &str, input: &[u8], output: &mut [u8], clock: &mut Deadline) -> i32 {
    if req.is_empty() || req.len() > HOST_REQ_MAX || req.as_bytes().contains(&0) || input.len() > DECL_MAX {
        return INVAL;
    }
    let a = crate::api();
    let h = loop {
        let error = clock.expired();
        if error != 0 { return error; }
        let h = (a.host_open)(req.as_ptr(), req.len() as u32);
        if h >= 0 { break h; }
        if h == STALE { return HOSTDOWN; }
        if h != AGAIN && h != FULL { return h; }
        let error = clock.wait(h);
        if error != 0 { return error; }
    };
    clock.progress = (a.get_tick)();
    let result = (|| {
        let mut sent = 0;
        while sent < input.len() {
            let error = clock.expired();
            if error != 0 { return error; }
            let n = (a.host_write)(h, input[sent..].as_ptr(), (input.len() - sent).min(WRITE_CHUNK) as u32);
            if n > 0 { sent += n as usize; clock.progress = (a.get_tick)(); }
            else if n == AGAIN || n == FULL {
                let error = clock.wait(n); if error != 0 { return error; }
            } else { return if n == 0 { IO } else { n }; }
        }
        let mut status = 0;
        let mut length = 0;
        loop {
            let error = clock.expired();
            if error != 0 { return error; }
            let rc = (a.host_status)(h, &mut status, &mut length);
            if rc == 0 { break; }
            if rc != AGAIN { return rc; }
            let error = clock.wait(rc); if error != 0 { return error; }
        }
        clock.progress = (a.get_tick)();
        if length as usize > output.len() { return NOSPC; }
        let mut received = 0;
        while received < length as usize {
            let error = clock.expired();
            if error != 0 { return error; }
            let n = (a.host_read)(h, output[received..].as_mut_ptr(), length - received as u32);
            if n > 0 { received += n as usize; clock.progress = (a.get_tick)(); }
            else if n == AGAIN {
                let error = clock.wait(n); if error != 0 { return error; }
            } else { return if n == 0 { IO } else { n }; }
        }
        received as i32
    })();
    let closed = (a.host_close)(h);
    if result >= 0 && closed < 0 { closed } else { result }
}

struct Registry { serial: u32, generation: u32, body: Vec<u8> }
struct RegistryCell(UnsafeCell<Option<Registry>>);
// OS32 runs one application with cooperative switching, like ApiHolder.
unsafe impl Sync for RegistryCell {}
static REGISTRY: RegistryCell = RegistryCell(UnsafeCell::new(None));

fn number(text: &str) -> Result<u32, i32> { text.parse().map_err(|_| IO) }
fn registry(body: Vec<u8>, serial: u32) -> Result<Registry, i32> {
    let text = core::str::from_utf8(&body).map_err(|_| IO)?;
    let mut lines = text.lines();
    let first = lines.next().ok_or(IO)?.strip_prefix("generation ").ok_or(IO)?;
    let generation = number(first)?;
    if generation == 0 { return Err(IO); }
    for line in lines {
        let fields: Vec<_> = line.split_whitespace().collect();
        if fields.len() != 4 || !valid_name(fields[0]) || number(fields[1]).is_err()
            || !(1..65535).contains(&number(fields[2])?)
            || !matches!(fields[3], "builtin" | "plugin" | "sfs" | "stub") { return Err(IO); }
    }
    Ok(Registry { serial, generation, body })
}
fn valid_name(name: &str) -> bool {
    !name.is_empty() && name.bytes().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'.')
}

/// Resolve a LIST name and read only that service's notice. The packet is the
/// 256-byte os32_host_notice.h layout, matching generated KAPI's byte pointer.
/// A serial change (including a new ALIVE) refreshes LIST without consuming any
/// unrelated notice. STALE invalidates and retries LIST once.
pub fn notice_read(name: &str, output: &mut [u8; 256]) -> i32 {
    if !valid_name(name) { return INVAL; }
    unsafe {
        for _ in 0..2 {
            let serial = (crate::api().host_notice_serial)();
            if (*REGISTRY.0.get()).as_ref().map(|r| r.serial) != Some(serial) {
                *REGISTRY.0.get() = None;
                let mut body = vec![0; DECL_MAX];
                let n = loop {
                    let n = host_call("LIST", &[], &mut body, 1000);
                    if n != NOSPC { break n; }
                    let size = match body.len().checked_mul(2) {
                        Some(size) => size, None => return NOSPC,
                    };
                    body.resize(size, 0);
                };
                if n < 0 { return n; }
                body.truncate(n as usize);
                match registry(body, serial) {
                    Ok(r) => *REGISTRY.0.get() = Some(r), Err(e) => return e,
                }
            }
            let r = (*REGISTRY.0.get()).as_ref().unwrap();
            let text = core::str::from_utf8(&r.body).unwrap();
            let id = text.lines().skip(1).find_map(|line| {
                let mut fields = line.split_whitespace();
                if fields.next() != Some(name) { return None; }
                fields.next(); fields.next().and_then(|n| n.parse::<u32>().ok())
            });
            let id = match id { Some(id) => id, None => return NOTFOUND };
            let rc = (crate::api().host_notice_read)(r.generation, id, output.as_mut_ptr());
            if rc != STALE { return rc; }
            *REGISTRY.0.get() = None;
        }
    }
    STALE
}

pub use notice_read as os32_host_notice_read;

/// Standard service helpers use the same deadline and error contract as host_call.
pub fn os32_host_time(out: &mut [u8], timeout: u32) -> i32 {
    host_call("os32.time GET", &[], out, timeout)
}
pub fn os32_host_get(url: &str, out: &mut [u8], timeout: u32) -> i32 {
    host_call(&format!("os32.http GET {}", url), &[], out, timeout)
}
pub fn os32_host_clip_get(out: &mut [u8], timeout: u32) -> i32 {
    host_call("os32.clip GET", &[], out, timeout)
}
pub fn os32_host_clip_put(input: &[u8], out: &mut [u8], timeout: u32) -> i32 {
    host_call(&format!("os32.clip PUT {}", input.len()), input, out, timeout)
}
pub fn os32_host_print_open(name: &str, out: &mut [u8], timeout: u32) -> i32 {
    host_call(&format!("os32.print OPEN {} text", name), &[], out, timeout)
}
pub fn os32_host_print_data(job: u32, input: &[u8], out: &mut [u8], timeout: u32) -> i32 {
    host_call(&format!("os32.print DATA {} {}", job, input.len()), input, out, timeout)
}
pub fn os32_host_print_close(job: u32, out: &mut [u8], timeout: u32) -> i32 {
    host_call(&format!("os32.print CLOSE {}", job), &[], out, timeout)
}
