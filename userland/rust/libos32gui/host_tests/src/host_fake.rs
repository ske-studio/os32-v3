//! host_fake.rs — C の `libos32host` とファイル継ぎ目 `hostsvc_file_*` の贋物
//! (ホスト TDD 専用、票 N4)。
//!
//! `hostsvc.rs` が `extern "C"` で呼ぶ host_* 6 本と hostsvc_file_* 3 本をここで
//! 定義して試験のリンクを閉じる。実サービスも実ファイルも出てこない — 記録
//! するのは引数、返すのは試験が仕込んだ本文 / 戻り値だけ。
//!
//! 状態は `thread_local!` なので cargo の並列試験でも混ざらない。`reset()` は
//! ついでに `crate::fake::reset()` を呼んで `kapi` を非 NULL にする
//! (`hostsvc` の `kapi_ready()` 門を開ける。cfg 側の `Once` を再利用するので
//! `kapi` 書きは 1 回に直列化される)。

use std::cell::RefCell;
use std::ffi::c_void;

#[derive(Default)]
struct State {
    /* --- 仕込み --- */
    get_rc: i32,
    get_body: Vec<u8>,
    get_http: i32,
    get_chunks: Vec<u32>, /* 空 = 本文を 1 チャンクで。設定すると可変分割 */
    print_rc: i32,
    print_pages: u32,
    print_svc: u32,
    clip_get_rc: i32,
    clip_get_body: Vec<u8>,
    clip_get_svc: u32,
    clip_get_chunks: Vec<u32>, /* 空 = 1 チャンク。設定すると可変分割 */
    clip_put_rc: i32,
    clip_put_svc: u32,
    time_rc: i32,
    time_str: Vec<u8>,
    /* ファイル継ぎ目 */
    file_open_rc: i32,     /* fd (>=0) or 負のエラー */
    file_data: Vec<u8>,    /* read が返す中身 */
    file_read_err: Option<i32>, /* Some(負) を仕込むと read が失敗 */
    file_read_pos: usize,
    /* --- 記録 --- */
    log: Vec<Call>,
    last_url: Vec<u8>,
    last_name: Vec<u8>,
    last_print_body: Vec<u8>,
    last_clip_put: Vec<u8>,
    last_path: Vec<u8>,
    last_open_fd: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Call {
    Get,
    PrintText,
    PrintStream,
    ClipGet,
    ClipPut,
    Time,
    FileOpen,
    FileRead,
    FileClose,
}

thread_local! {
    static ST: RefCell<State> = RefCell::new(State::default());
}

fn with<R>(f: impl FnOnce(&mut State) -> R) -> R {
    ST.with(|s| f(&mut s.borrow_mut()))
}

/* ---------------- 試験から使う口 ---------------- */

pub fn reset() {
    /* cfg 側の Once が kapi を armed にする (kapi_ready 門を開ける)。 */
    crate::fake::reset();
    with(|s| *s = State::default());
}

pub fn set_get(rc: i32, body: &[u8], http: i32) {
    with(|s| {
        s.get_rc = rc;
        s.get_body = body.to_vec();
        s.get_http = http;
        s.get_chunks = Vec::new();
    });
}
/// 本文を `chunks` の並び (バイト数) で sink へ小分けに渡す。実サービスは
/// 到着ぶんを何回にも分けて sink する — cap 到達後も sink が受理し続ける
/// (0 を返す) ことを確かめるため、cap をまたぐ分割を仕込めるようにする。
pub fn set_get_chunked(rc: i32, body: &[u8], http: i32, chunks: &[u32]) {
    with(|s| {
        s.get_rc = rc;
        s.get_body = body.to_vec();
        s.get_http = http;
        s.get_chunks = chunks.to_vec();
    });
}
pub fn set_print(rc: i32, pages: u32, svc: u32) {
    with(|s| {
        s.print_rc = rc;
        s.print_pages = pages;
        s.print_svc = svc;
    });
}
pub fn set_clip_get(rc: i32, body: &[u8], svc: u32) {
    with(|s| {
        s.clip_get_rc = rc;
        s.clip_get_body = body.to_vec();
        s.clip_get_svc = svc;
        s.clip_get_chunks = Vec::new();
    });
}
/// クリップボード本文を `chunks` の並びで sink へ小分けに渡す (set_get_chunked と同旨)。
pub fn set_clip_get_chunked(rc: i32, body: &[u8], svc: u32, chunks: &[u32]) {
    with(|s| {
        s.clip_get_rc = rc;
        s.clip_get_body = body.to_vec();
        s.clip_get_svc = svc;
        s.clip_get_chunks = chunks.to_vec();
    });
}
pub fn set_clip_put(rc: i32, svc: u32) {
    with(|s| {
        s.clip_put_rc = rc;
        s.clip_put_svc = svc;
    });
}
pub fn set_time(rc: i32, s_str: &[u8]) {
    with(|s| {
        s.time_rc = rc;
        s.time_str = s_str.to_vec();
    });
}
pub fn set_file(open_rc: i32, data: &[u8], read_err: Option<i32>) {
    with(|s| {
        s.file_open_rc = open_rc;
        s.file_data = data.to_vec();
        s.file_read_err = read_err;
        s.file_read_pos = 0;
    });
}

pub fn log() -> Vec<Call> {
    with(|s| s.log.clone())
}
pub fn last_url() -> Vec<u8> {
    with(|s| s.last_url.clone())
}
pub fn last_name() -> Vec<u8> {
    with(|s| s.last_name.clone())
}
pub fn last_print_body() -> Vec<u8> {
    with(|s| s.last_print_body.clone())
}
pub fn last_clip_put() -> Vec<u8> {
    with(|s| s.last_clip_put.clone())
}
pub fn last_path() -> Vec<u8> {
    with(|s| s.last_path.clone())
}

/* ---------------- 小道具 ---------------- */

unsafe fn cstr(p: *const u8) -> Vec<u8> {
    if p.is_null() {
        return Vec::new();
    }
    let mut v = Vec::new();
    let mut i = 0isize;
    while *p.offset(i) != 0 {
        v.push(*p.offset(i));
        i += 1;
        assert!(i < 8192, "NUL 終端されていない文字列を渡された");
    }
    v
}

type SinkFn = extern "C" fn(*mut c_void, *const u8, u32) -> i32;
type SrcFn = extern "C" fn(*mut c_void, *mut u8, u32) -> i32;

/// 本文を `chunks` (空なら全量 1 本) の並びで `sink` へ渡し、**受理できた実長**
/// を返す。sink が非 0 を返したら中断で `None` (実サービスの EABORT に相当)。
/// 実長は sink が cap を超えて捨てても数え続ける — その挙動を検査で踏むための
/// 可変分割。
unsafe fn deliver_chunked(
    sink: SinkFn,
    ud: *mut c_void,
    body: &[u8],
    chunks: &[u32],
) -> Option<u32> {
    let mut received: u32 = 0;
    let mut off = 0usize;
    let plan: Vec<usize> = if chunks.is_empty() {
        vec![body.len()]
    } else {
        chunks.iter().map(|&c| c as usize).collect()
    };
    for csz in plan {
        if off >= body.len() {
            break;
        }
        let end = (off + csz).min(body.len());
        if end > off {
            let r = sink(ud, body[off..].as_ptr(), (end - off) as u32);
            if r != 0 {
                return None; /* sink が中断 → EABORT */
            }
            received += (end - off) as u32;
        }
        off = end;
    }
    /* チャンク表が本文を覆い切らなければ残りを最後に 1 本で流す */
    if off < body.len() {
        let r = sink(ud, body[off..].as_ptr(), (body.len() - off) as u32);
        if r != 0 {
            return None;
        }
        received += (body.len() - off) as u32;
    }
    Some(received)
}

/* ---------------- libos32host の贋物 ---------------- */

#[no_mangle]
pub unsafe extern "C" fn host_time(out: *mut u8) -> i32 {
    let (rc, s) = with(|s| {
        s.log.push(Call::Time);
        (s.time_rc, s.time_str.clone())
    });
    if rc >= 0 && !out.is_null() {
        for (i, b) in s.iter().enumerate() {
            *out.add(i) = *b;
        }
        *out.add(s.len()) = 0;
    }
    rc
}

#[no_mangle]
pub unsafe extern "C" fn host_get(
    url: *const u8,
    sink: SinkFn,
    ud: *mut c_void,
    http_status: *mut i32,
    nbytes: *mut u32,
) -> i32 {
    let u = cstr(url);
    let (rc, body, http, chunks) = with(|s| {
        s.log.push(Call::Get);
        s.last_url = u;
        (s.get_rc, s.get_body.clone(), s.get_http, s.get_chunks.clone())
    });
    /* http_status は read ループの前に書く (契約)。 */
    if !http_status.is_null() {
        *http_status = http;
    }
    /* 本文を (可変) チャンクで sink へ。sink が非 0 を返したら EABORT。 */
    match deliver_chunked(sink, ud, &body, &chunks) {
        None => -106, /* HOST_EABORT */
        Some(delivered) => {
            if !nbytes.is_null() {
                *nbytes = delivered;
            }
            rc
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn host_print_text(
    name: *const u8,
    buf: *const u8,
    len: u32,
    pages: *mut u32,
    svc_status: *mut u32,
) -> i32 {
    let nm = cstr(name);
    let mut body = Vec::new();
    if !buf.is_null() {
        for i in 0..len as isize {
            body.push(*buf.offset(i));
        }
    }
    let (rc, pg, svc) = with(|s| {
        s.log.push(Call::PrintText);
        s.last_name = nm;
        s.last_print_body = body;
        (s.print_rc, s.print_pages, s.print_svc)
    });
    if !pages.is_null() {
        *pages = pg;
    }
    if !svc_status.is_null() {
        *svc_status = svc;
    }
    rc
}

#[no_mangle]
pub unsafe extern "C" fn host_print_stream(
    name: *const u8,
    src: SrcFn,
    ud: *mut c_void,
    pages: *mut u32,
    svc_status: *mut u32,
) -> i32 {
    let nm = cstr(name);
    with(|s| {
        s.log.push(Call::PrintStream);
        s.last_name = nm;
    });
    /* src から EOF (0) まで引く。負は EABORT (pages は据置き)。 */
    let mut collected = Vec::new();
    let mut chunk = [0u8; 4096];
    loop {
        let n = src(ud, chunk.as_mut_ptr(), chunk.len() as u32);
        if n < 0 {
            with(|s| s.last_print_body = collected.clone());
            return -106; /* HOST_EABORT */
        }
        if n == 0 {
            break;
        }
        collected.extend_from_slice(&chunk[..n as usize]);
    }
    let (rc, pg, svc) = with(|s| {
        s.last_print_body = collected;
        (s.print_rc, s.print_pages, s.print_svc)
    });
    if !pages.is_null() {
        *pages = pg;
    }
    if !svc_status.is_null() {
        *svc_status = svc;
    }
    rc
}

#[no_mangle]
pub unsafe extern "C" fn host_clip_get(
    sink: SinkFn,
    ud: *mut c_void,
    nbytes: *mut u32,
    svc_status: *mut u32,
) -> i32 {
    let (rc, body, svc, chunks) = with(|s| {
        s.log.push(Call::ClipGet);
        (s.clip_get_rc, s.clip_get_body.clone(), s.clip_get_svc, s.clip_get_chunks.clone())
    });
    if !svc_status.is_null() {
        *svc_status = svc;
    }
    match deliver_chunked(sink, ud, &body, &chunks) {
        None => -106,
        Some(delivered) => {
            if !nbytes.is_null() {
                *nbytes = delivered;
            }
            rc
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn host_clip_put(buf: *const u8, len: u32, svc_status: *mut u32) -> i32 {
    let mut b = Vec::new();
    if !buf.is_null() {
        for i in 0..len as isize {
            b.push(*buf.offset(i));
        }
    }
    let (rc, svc) = with(|s| {
        s.log.push(Call::ClipPut);
        s.last_clip_put = b;
        (s.clip_put_rc, s.clip_put_svc)
    });
    if !svc_status.is_null() {
        *svc_status = svc;
    }
    rc
}

/* ---------------- ファイル継ぎ目 hostsvc_file_* の贋物 ---------------- */

#[no_mangle]
pub unsafe extern "C" fn hostsvc_file_open(path: *const u8) -> i32 {
    let p = cstr(path);
    with(|s| {
        s.log.push(Call::FileOpen);
        s.last_path = p;
        s.file_read_pos = 0;
        s.last_open_fd = s.file_open_rc;
        s.file_open_rc
    })
}

#[no_mangle]
pub unsafe extern "C" fn hostsvc_file_read(_fd: i32, buf: *mut u8, cap: u32) -> i32 {
    with(|s| {
        s.log.push(Call::FileRead);
        if let Some(e) = s.file_read_err {
            return e;
        }
        let remaining = s.file_data.len() - s.file_read_pos;
        if remaining == 0 {
            return 0; /* EOF */
        }
        let n = remaining.min(cap as usize);
        for i in 0..n {
            *buf.add(i) = s.file_data[s.file_read_pos + i];
        }
        s.file_read_pos += n;
        n as i32
    })
}

#[no_mangle]
pub unsafe extern "C" fn hostsvc_file_close(_fd: i32) {
    with(|s| s.log.push(Call::FileClose));
}
