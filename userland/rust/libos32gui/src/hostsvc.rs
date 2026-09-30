//! hostsvc.rs — Host Services (`libos32host`) を GUI アプリへ薄く公開する窓口。
//!
//! 票 `docs/archive/network/TASK_N4.md` §1 (N4a、基盤)。
//!
//! ```text
//!   アプリ ──(ジャンプ表 105..=110)──> ここ ──(extern "C")──> libos32host.a (C)
//! ```
//!
//! 鉄則 (cfgro.rs と同流儀):
//! - **薄い**。AGAIN ループ・宣言長・ストリーミング・多段 PRINT の後始末は
//!   libos32host が既に隠す。ここは ptr+len を検証し、private な NUL バッファへ
//!   写し、C を 1 本呼ぶだけ。エラーは `HOST_E*` をそのまま i32 で返す
//!   (GUI 側が文言化。`OS32_ERR_*` ではない)。
//! - **`kapi` は `crate::cfgro` の `#[no_mangle] static mut kapi` を共用する。
//!   ここで再定義しない** — shlib リンクは `--allow-multiple-definition` なので
//!   重複が黙って通り、libos32host が NULL の kapi を読む事故になる。各 wrapper
//!   は `cfgro::kapi_ready()` の門を通す (`os32gui_shlib_init` が入れるまで NULL)。
//! - ptr+len の検査・NUL 終端写し・UTF-8 境界は `crate::cfgro` /
//!   `crate::utf8core` と共用する (票 §5)。
//!
//! **os32api を名指ししない** — `crate::hostabi` (本体では
//! `pub use os32api::host`、ホスト TDD では `#[path]` の写し) 経由で
//! libos32host を呼ぶ。これで host_tests は os32api を丸ごと組まずに
//! このファイルを `#[path]` 取り込みできる (cfgro.rs と同じ)。
//!
//! ファイル I/O (print_file) は差し替えられる継ぎ目 `hostsvc_file_*` にしてある。
//! 本体は `crate::hostsvc_file` (os32api の sys_open/read/close)、ホスト TDD は
//! 贋物を並べる。
#![allow(dead_code)]

use core::cmp::min;
use core::ffi::c_void;
use core::ptr;

use crate::cfgro::{checked_slice, copy_cstr, kapi_ready, raw_out_ok};
use crate::hostabi::{
    host_clip_get, host_clip_put, host_get, host_print_stream, host_print_text, host_time,
};
use crate::utf8core::utf8_truncate;

/* ================================================================ */
/*  エラー番号 (正典: userland/lib/host/libos32host.h)               */
/*                                                                  */
/*  ここは自己完結のための写し。`shlib.rs` に `os32api::host` と      */
/*  一致することを見る const assert を置いてある (ずれたらビルドが    */
/*  止まる。cfgro.rs と同じ)。                                        */
/* ================================================================ */
pub const HOST_ELINK: i32 = -100;
pub const HOST_ENODEV: i32 = -101;
pub const HOST_ETIMEOUT: i32 = -102;
pub const HOST_ESERVICE: i32 = -103;
pub const HOST_EINVAL: i32 = -104;
pub const HOST_EIO: i32 = -105;
pub const HOST_EABORT: i32 = -106;

/* ---- ファイル継ぎ目 (`hostsvc_file.rs`) の open 失敗コード ----------- */
/*  正典は sdk/include/os32/os32_kapi_shared.h。ここは畳みに使う写し。   */
/// ディレクトリを開こうとした (`OS32_ERR_ISDIR`)。
const FILE_ERR_ISDIR: i32 = -8;
/// 引数不正 (`OS32_ERR_INVAL`)。
const FILE_ERR_INVAL: i32 = -9;

/* ================================================================ */
/*  寸法 ([C4] の管理元は libos32host.h)                             */
/* ================================================================ */
/// host_open 要求行の上限 (`HOST_REQ_MAX`)。
const HOST_REQ_MAX: usize = 1400;
/// url を写す private バッファ (NUL 込み、票 §1: `HOST_REQ_MAX−4`)。
const URL_CAP: usize = HOST_REQ_MAX - 4;
/// 印刷名を写す private バッファ (NUL 込み)。
const NAME_CAP: usize = 256;
/// 印刷名の最大バイト数 (NUL を除く)。
const NAME_MAX: usize = NAME_CAP - 1;
/// ファイルパスを写す private バッファ (NUL 込み、票 §1: 256B)。
const PATH_CAP: usize = 256;
/// ファイルパスの最大バイト数 (NUL を除く)。
const PATH_MAX: usize = PATH_CAP - 1;
/// CLIP PUT の上限 (`HOST_CLIP_MAX`)。
const CLIP_MAX: usize = 4096;

/* ================================================================ */
/*  差し替えられるファイル I/O の継ぎ目 (print_file 用)              */
/*                                                                  */
/*  本体は `crate::hostsvc_file` が `#[no_mangle]` で定義 (os32api の  */
/*  sys_open/read/close)。ホスト TDD は贋物を並べる。だからこの       */
/*  ファイルは os32api を名指しせずに済む。                          */
/* ================================================================ */
extern "C" {
    /// パス (NUL 終端) を RO で開く。fd (>=0) / 負の `OS32_ERR_*`。
    fn hostsvc_file_open(path: *const u8) -> i32;
    /// `fd` から最大 `cap` バイト読む。読めた数 (>=0、0 = EOF) / 負のエラー。
    fn hostsvc_file_read(fd: i32, buf: *mut u8, cap: u32) -> i32;
    /// `fd` を閉じる (全経路で呼ぶ)。
    fn hostsvc_file_close(fd: i32);
}

/* ================================================================ */
/*  純粋部 — 検査と写し (ホスト TDD がここを直接叩く)                 */
/* ================================================================ */

/// `src` を NUL 終端して `dst` へ写す。**制御バイト (< 0x20 と 0x7F) は落とす**
/// (票 §1: 印刷名の制御文字を落とす)。埋め込み NUL も飛ばす。入りきらなければ
/// `false` (NUL の 1 バイトを含めて `dst` に収まること)。
///
/// **空名は `false`** (N4a 実装レビュー nb3): 空 basename・制御文字のみの name は
/// 印刷ジョブ名にできないので `HOST_EINVAL` で呼び手へ返す (lpr は空を "file" に
/// 既定するが、GUI は明快に弾く)。空白 (0x20) は制御文字ではないので残り、名前と
/// して通る (Agent 側も空白名を許す)。
pub fn copy_name(src: &[u8], dst: &mut [u8]) -> bool {
    if dst.is_empty() {
        return false;
    }
    let mut j = 0usize;
    let mut i = 0usize;
    while i < src.len() {
        let c = src[i];
        i += 1;
        if c < 0x20 || c == 0x7F {
            continue; /* 制御バイトは落とす */
        }
        if j + 1 >= dst.len() {
            return false; /* NUL の余地が要る */
        }
        dst[j] = c;
        j += 1;
    }
    if j == 0 {
        return false; /* 空名 / 制御文字のみ → EINVAL */
    }
    dst[j] = 0;
    true
}

/// host_get / clip_get の out 版 sink が持つ状態。
///
/// `out[0..cap]` へ受信本文を詰めながら、**cap 到達後も捨てて数え続ける**
/// (票 §5: 実長を `nbytes` で知るため sink は 0 を返し続ける)。
struct OutSink {
    out: *mut u8,
    cap: usize,
    written: usize,
}

/// print_file の src が読むファイル。
struct FileSrc {
    fd: i32,
}

/// out 版 sink。`out` に `min(残り cap, n)` だけ写し、常に 0 を返す
/// (受理し続ける = 実長は `host_*` の `nbytes` が数える)。
extern "C" fn out_sink(ud: *mut c_void, buf: *const u8, n: u32) -> i32 {
    if ud.is_null() {
        return 0;
    }
    let ctx = unsafe { &mut *(ud as *mut OutSink) };
    let take = min(ctx.cap - ctx.written, n as usize);
    if take > 0 && !buf.is_null() && !ctx.out.is_null() {
        unsafe {
            ptr::copy_nonoverlapping(buf, ctx.out.add(ctx.written), take);
        }
        ctx.written += take;
    }
    0
}

/// ファイル src。`buf[0..cap]` に読み、読めた数 / 0 (EOF) / 負 (中断) を返す。
extern "C" fn file_source(ud: *mut c_void, buf: *mut u8, cap: u32) -> i32 {
    if ud.is_null() {
        return -1;
    }
    let s = unsafe { &mut *(ud as *mut FileSrc) };
    unsafe { hostsvc_file_read(s.fd, buf, cap) }
}

/// open 失敗コードを畳む: 引数不正 / ディレクトリ → `HOST_EINVAL`、他 → `HOST_EIO`
/// (票 §1)。`rc` は負であること。
#[inline]
pub fn fold_open_err(rc: i32) -> i32 {
    if rc == FILE_ERR_INVAL || rc == FILE_ERR_ISDIR {
        HOST_EINVAL
    } else {
        HOST_EIO
    }
}

/* ================================================================ */
/*  105: os32gui_host_get                                           */
/* ================================================================ */

/// `os32gui_host_get(url, url_len, out, cap, http_status)` — url を GET し
/// `out(cap)` に受ける。
///
/// 戻り = **受信実長 (snprintf 流)**。`out` には `min(実長, cap)` を写す
/// (呼び手は `ret > cap` で切れたと判る)。本文はバイナリ可なので UTF-8 境界
/// 処理はしない。`http_status` (NULL 可) には HTTP ステータス。
#[no_mangle]
pub extern "C" fn os32gui_host_get(
    url: *const u8,
    url_len: u32,
    out: *mut u8,
    cap: u32,
    http_status: *mut u32,
) -> i32 {
    if !kapi_ready() || !raw_out_ok(out as *const u8, cap) {
        return HOST_EINVAL;
    }
    let u = match unsafe { checked_slice(url, url_len, (URL_CAP - 1) as u32) } {
        Some(s) => s,
        None => return HOST_EINVAL,
    };
    let mut ubuf = [0u8; URL_CAP];
    if !copy_cstr(u, &mut ubuf) {
        /* 空 url / 埋め込み NUL / 長すぎ */
        return HOST_EINVAL;
    }
    let mut ctx = OutSink { out, cap: cap as usize, written: 0 };
    let mut hs: i32 = 0;
    let mut nb: u32 = 0;
    let rc = unsafe {
        host_get(
            ubuf.as_ptr(),
            out_sink,
            &mut ctx as *mut OutSink as *mut c_void,
            &mut hs,
            &mut nb,
        )
    };
    if !http_status.is_null() {
        unsafe { ptr::write(http_status, hs as u32) }
    }
    if rc < 0 {
        return rc;
    }
    nb as i32
}

/* ================================================================ */
/*  106: os32gui_print_text                                         */
/* ================================================================ */

/// `os32gui_print_text(name, name_len, buf, len, pages, svc_status)` —
/// `buf`/`len` をホストへ印刷。`name` は制御文字を落として private NUL バッファ
/// へ写す。`pages` / `svc_status` は NULL 可。戻り = 0 / `HOST_E*`。
#[no_mangle]
pub extern "C" fn os32gui_print_text(
    name: *const u8,
    name_len: u32,
    buf: *const u8,
    len: u32,
    pages: *mut u32,
    svc_status: *mut u32,
) -> i32 {
    if !kapi_ready() {
        return HOST_EINVAL;
    }
    let nm = match unsafe { checked_slice(name, name_len, NAME_MAX as u32) } {
        Some(s) => s,
        None => return HOST_EINVAL,
    };
    /* 本文はバイナリ可。長さ上限はライブラリが宣言長で分割するので設けず、
     * NULL+len!=0 と番地の折り返しだけを見る (max = u32::MAX)。 */
    let body = match unsafe { checked_slice(buf, len, u32::MAX) } {
        Some(s) => s,
        None => return HOST_EINVAL,
    };
    let mut nbuf = [0u8; NAME_CAP];
    if !copy_name(nm, &mut nbuf) {
        return HOST_EINVAL;
    }
    unsafe { host_print_text(nbuf.as_ptr(), body.as_ptr(), len, pages, svc_status) }
}

/* ================================================================ */
/*  107: os32gui_print_file                                         */
/* ================================================================ */

/// `os32gui_print_file(name, name_len, path, path_len, pages, svc_status)` —
/// `path` を開いて `host_print_stream` でストリーミング印刷 (メモリ一定)。
///
/// open 失敗の畳み: 引数不正 / ディレクトリ = `HOST_EINVAL`、他 = `HOST_EIO`。
/// `src < 0` (ファイル read 失敗) は `HOST_EABORT` (CLOSE を送らない = job 放置、
/// N3 仕様)。**FD は全経路で close**。`pages` / `svc_status` は NULL 可。
#[no_mangle]
pub extern "C" fn os32gui_print_file(
    name: *const u8,
    name_len: u32,
    path: *const u8,
    path_len: u32,
    pages: *mut u32,
    svc_status: *mut u32,
) -> i32 {
    if !kapi_ready() {
        return HOST_EINVAL;
    }
    let nm = match unsafe { checked_slice(name, name_len, NAME_MAX as u32) } {
        Some(s) => s,
        None => return HOST_EINVAL,
    };
    let pth = match unsafe { checked_slice(path, path_len, PATH_MAX as u32) } {
        Some(s) => s,
        None => return HOST_EINVAL,
    };
    let mut nbuf = [0u8; NAME_CAP];
    if !copy_name(nm, &mut nbuf) {
        return HOST_EINVAL;
    }
    let mut pbuf = [0u8; PATH_CAP];
    if !copy_cstr(pth, &mut pbuf) {
        /* 空パス / 埋め込み NUL / 長すぎ */
        return HOST_EINVAL;
    }
    let fd = unsafe { hostsvc_file_open(pbuf.as_ptr()) };
    if fd < 0 {
        return fold_open_err(fd);
    }
    let mut src = FileSrc { fd };
    let rc = unsafe {
        host_print_stream(
            nbuf.as_ptr(),
            file_source,
            &mut src as *mut FileSrc as *mut c_void,
            pages,
            svc_status,
        )
    };
    unsafe { hostsvc_file_close(fd) };
    rc
}

/* ================================================================ */
/*  108: os32gui_clip_get                                           */
/* ================================================================ */

/// `os32gui_clip_get(out, cap, total, svc_status)` — ホストのクリップボードを
/// `out(cap)` に取る。
///
/// 戻り = **`out` に書いた長さ** (UTF-8 境界まで戻した後 = 呼び手が注入して
/// よいバイト数)。`total` (NULL 可) = 受信実長 (呼び手は `*total > ret` で
/// 切れたと判る)。**NUL 以降も捨てる** (`utf8_truncate` が NUL で止まる)。
/// `out` は NUL 終端しない。`cap == 0` / `out == NULL` → `HOST_EINVAL`。
/// `svc_status` (NULL 可、backend 無し = 503)。
#[no_mangle]
pub extern "C" fn os32gui_clip_get(
    out: *mut u8,
    cap: u32,
    total: *mut u32,
    svc_status: *mut u32,
) -> i32 {
    if !kapi_ready() || out.is_null() || cap == 0 || !raw_out_ok(out as *const u8, cap) {
        return HOST_EINVAL;
    }
    let mut ctx = OutSink { out, cap: cap as usize, written: 0 };
    let mut nb: u32 = 0;
    let rc = unsafe {
        host_clip_get(
            out_sink,
            &mut ctx as *mut OutSink as *mut c_void,
            &mut nb,
            svc_status,
        )
    };
    if !total.is_null() {
        unsafe { ptr::write(total, nb) }
    }
    if rc < 0 {
        return rc;
    }
    /* out に実際に入ったのは min(実長, cap)。そこから UTF-8 境界まで戻す
     * (NUL 以降も捨てる)。 */
    let stored = min(nb as usize, cap as usize);
    let filled = unsafe { core::slice::from_raw_parts(out, stored) };
    utf8_truncate(filled, cap as usize) as i32
}

/* ================================================================ */
/*  109: os32gui_clip_put                                           */
/* ================================================================ */

/// `os32gui_clip_put(buf, len, svc_status)` — `buf`/`len` (1〜4096) をホストの
/// クリップボードへ。0 / 4096 超は `HOST_EINVAL`。`svc_status` は NULL 可。
#[no_mangle]
pub extern "C" fn os32gui_clip_put(buf: *const u8, len: u32, svc_status: *mut u32) -> i32 {
    if !kapi_ready() {
        return HOST_EINVAL;
    }
    if len == 0 || len as usize > CLIP_MAX {
        return HOST_EINVAL;
    }
    let b = match unsafe { checked_slice(buf, len, CLIP_MAX as u32) } {
        Some(s) => s,
        None => return HOST_EINVAL,
    };
    unsafe { host_clip_put(b.as_ptr(), len, svc_status) }
}

/* ================================================================ */
/*  110: os32gui_host_time                                          */
/* ================================================================ */

/// `os32gui_host_time(out20)` — ホスト時刻を `out` (20B) に NUL 終端で書く。
/// 戻り = 0 / `HOST_E*`。
#[no_mangle]
pub extern "C" fn os32gui_host_time(out: *mut u8) -> i32 {
    if !kapi_ready() || out.is_null() {
        return HOST_EINVAL;
    }
    unsafe { host_time(out) }
}
