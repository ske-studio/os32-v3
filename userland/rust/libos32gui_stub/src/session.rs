//! session.rs — gshell へのセッション要求 (契約 V12-S) のスタブ。
//!
//! 外部アプリは `exec_run()` を自分で呼ばない。別アプリ起動 / CUI 復帰 /
//! system halt は `GUI_OP_SESSION_REQUEST` (op 66) で gshell へ**依頼**する。
//!
//! `Ok(())` は **pending として受理された**だけで完了ではない (契約 S4)。
//! 受理されたらふつうは直後に `GUI_EV_QUIT` が届き、U3 ループが
//! `App::on_quit` (既定は `ui.quit()`) を通って戻る。完了 callback は無い。
//!
//! - `ERR_INVAL`: action / path が不正 (path 検査はライブラリ側が WM を呼ぶ前に行う)
//! - `ERR_FULL`: 既に別の SessionAction が pending
//! - `ERR_NOSYS`: op 66 を知らない古い gshell

use crate::client::{ok0, GuiResult};
use crate::shcall;
use os32api::gui::proto::{GUI_SESSION_LAUNCH, GUI_SESSION_SHUTDOWN, GUI_SESSION_SWITCH_CUI};
use os32api::gui::stub as sh;

/// `LAUNCH` の path に載る最大バイト数 (契約 S4)。
pub const SESSION_PATH_MAX: usize = 255;

/// SessionAction を 1 件依頼する (契約 S4)。ふつうは下の 3 つを使う。
pub fn session_request(action: u8, value: &[u8]) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_SESSION_REQUEST,
        extern "C" fn(u32, *const u8, u32) -> i32,
        action as u32,
        value.as_ptr(),
        value.len() as u32
    ))
}

/// 別の GUI アプリを起動してもらう (契約 S2 / F1)。`path` は 1〜255B の絶対パス。
pub fn session_launch(path: &[u8]) -> GuiResult<()> {
    session_request(GUI_SESSION_LAUNCH, path)
}

/// CUI モードへ戻してもらう (契約 S6)。確認ダイアログは gshell 側の責任。
pub fn session_switch_cui() -> GuiResult<()> {
    session_request(GUI_SESSION_SWITCH_CUI, b"")
}

/// system halt を依頼する (契約 S7)。電源 OFF ではない。
pub fn session_shutdown() -> GuiResult<()> {
    session_request(GUI_SESSION_SHUTDOWN, b"")
}
