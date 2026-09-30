//! host.rs — Host Services の C ライブラリ `libos32host` への **ABI 宣言の
//! 1 か所** (票 `docs/archive/network/TASK_N4.md` §1)。
//!
//! シグネチャと定数の**正典は `userland/lib/host/libos32host.h`** (N3)。ここは
//! その Rust 写しで、値を勝手に決めない。
//!
//! ```text
//!   libos32gui::hostsvc (shlib) ──(extern "C")──> libos32host.a (C)
//! ```
//!
//! 置き場をここに集めた理由 (cfg.rs / 票 S4 §4 と同じ):
//!
//! - 同じ ABI の宣言を 2 か所に置くと、片方だけ直ったときに**リンクは通るのに
//!   引数がずれる**。宣言が 1 か所なら食い違えない。
//! - `#[link]` 属性は**付けない**。リンクするライブラリを決めるのは
//!   `build/programs.mk` の側 (shlib は `libos32gui.shlib` の規則で
//!   `libos32host.a` を静的リンク)。
//! - 属性の無い未使用の `extern "C"` 宣言は、**呼び出しもアドレス参照も無ければ
//!   未解決シンボルにならない**。libos32host.a を持たない利用者 (stub /
//!   ホスト試験) でもそのまま取り込める。
//!
//! このファイルは `os32api` の**他のモジュールに依存しない** — `core` だけを
//! 使う。`libos32gui` のホスト TDD (`host_tests`) が `#[path]` で直に取り込み、
//! `host_*` の贋物を並べて `hostsvc.rs` の分岐を固定するため。

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
