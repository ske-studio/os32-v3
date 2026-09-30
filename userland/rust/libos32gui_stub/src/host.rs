//! host.rs — Host Services (`libos32host`) のスタブ (票 N4 §1)。
//!
//! アプリは libos32host.a も host_* KAPI (210..=214) も直に触らない。**この
//! 6 本だけ**を使い、shlib のジャンプ表 105..=110 を通して OS 側の
//! `libos32gui::hostsvc` に入る (N4b のファイラ印刷・端末コピペはここを呼ぶ)。
//!
//! 契約:
//! - 業務ステータス (GET の HTTP、print/clip の 4xx/5xx) はエラーにせず out
//!   引数で返す。関数の戻り値は `HOST_E*` (負) をそのまま透過する。
//! - `pages` / `svc` / `http_status` は `Option` で NULL を表す。
//! - GUI 配下では host_* の待ち (`sys_yield`) は park になり WM が回る
//!   (印刷 / 取得中はそのアプリの UI が固まるが present は続く)。

use crate::client::{GuiErr, GuiResult};
use crate::shcall;
use os32api::gui::stub as sh;

/// `Option<&mut u32>` を `*mut u32` へ (None は NULL)。
#[inline]
fn optr(o: Option<&mut u32>) -> *mut u32 {
    match o {
        Some(r) => r as *mut u32,
        None => core::ptr::null_mut(),
    }
}

/// `url` を GET し `out` に受ける。
///
/// 戻り = **受信実長 (snprintf 流)**。`out` には `min(実長, out.len())` が入るので、
/// 呼び手は `ret as usize > out.len()` で切れたと判る。負は `HOST_E*`。
/// `http_status` (NULL 可) に HTTP ステータス。本文はバイナリ可。
pub fn host_get(url: &[u8], out: &mut [u8], http_status: Option<&mut u32>) -> i32 {
    shcall!(
        sh::E_HOST_GET,
        extern "C" fn(*const u8, u32, *mut u8, u32, *mut u32) -> i32,
        url.as_ptr(),
        url.len() as u32,
        out.as_mut_ptr(),
        out.len() as u32,
        optr(http_status)
    )
}

/// `buf` を `name` で印刷。戻り = 0 / `HOST_E*`。`pages` = 枚数、`svc` = 業務値
/// (409/500)、どちらも NULL 可。
pub fn print_text(
    name: &[u8],
    buf: &[u8],
    pages: Option<&mut u32>,
    svc: Option<&mut u32>,
) -> i32 {
    shcall!(
        sh::E_PRINT_TEXT,
        extern "C" fn(*const u8, u32, *const u8, u32, *mut u32, *mut u32) -> i32,
        name.as_ptr(),
        name.len() as u32,
        buf.as_ptr(),
        buf.len() as u32,
        optr(pages),
        optr(svc)
    )
}

/// `path` のファイルを `name` で印刷 (ストリーミング、メモリ一定)。
/// 戻り = 0 / `HOST_E*`。`pages` / `svc` は NULL 可。
pub fn print_file(
    name: &[u8],
    path: &[u8],
    pages: Option<&mut u32>,
    svc: Option<&mut u32>,
) -> i32 {
    shcall!(
        sh::E_PRINT_FILE,
        extern "C" fn(*const u8, u32, *const u8, u32, *mut u32, *mut u32) -> i32,
        name.as_ptr(),
        name.len() as u32,
        path.as_ptr(),
        path.len() as u32,
        optr(pages),
        optr(svc)
    )
}

/// ホストのクリップボードを `out` に取る。
///
/// `Ok((written, total))`: `written` = `out` に入り**注入してよい**バイト数
/// (UTF-8 境界まで戻した後)、`total` = 受信実長 (`total > written` なら切れた)。
/// `out` は NUL 終端されない。`out` が空だと `Err(EINVAL 相当)`。
pub fn clip_get(out: &mut [u8]) -> GuiResult<(usize, usize)> {
    let mut total: u32 = 0;
    let r = shcall!(
        sh::E_CLIP_GET,
        extern "C" fn(*mut u8, u32, *mut u32, *mut u32) -> i32,
        out.as_mut_ptr(),
        out.len() as u32,
        &mut total as *mut u32,
        core::ptr::null_mut()
    );
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok((r as usize, total as usize))
    }
}

/// `buf` (1〜4096) をホストのクリップボードへ。戻り = 0 / `HOST_E*` (0 / 4096 超は
/// `HOST_EINVAL`)。`svc` は NULL 可。
pub fn clip_put(buf: &[u8], svc: Option<&mut u32>) -> i32 {
    shcall!(
        sh::E_CLIP_PUT,
        extern "C" fn(*const u8, u32, *mut u32) -> i32,
        buf.as_ptr(),
        buf.len() as u32,
        optr(svc)
    )
}

/// ホスト時刻を `out` (20B) に NUL 終端で書く。戻り = 0 / `HOST_E*`。
pub fn host_time(out: &mut [u8; 20]) -> i32 {
    shcall!(
        sh::E_HOST_TIME,
        extern "C" fn(*mut u8) -> i32,
        out.as_mut_ptr()
    )
}
