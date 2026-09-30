//! modal.rs — 標準ダイアログ (契約 V12-C / V12-M) のスタブ。実体はライブラリ側。
//!
//! 公開する形は `libos32gui::modal` とまったく同じ (アプリのソースは無変更)。
//!
//! # 使い方 (入れ子ループを作らない)
//!
//! ```text
//!   let id = modal::file_open(win.id(), b"Open file")?;   // 開くだけ
//!   ...
//!   fn on_modal(&mut self, ui, dialog, result) {          // 完了イベント
//!       let mut buf = [0u8; 256];
//!       if let Ok(r) = modal::modal_result(dialog, &mut buf) { /* buf[..r.copied] */ }
//!   }
//! ```
//!
//! [`modal_result`] は **`on_modal` を受けてから呼ぶ API**。`OS32_ERR_AGAIN` は
//! プロトコルに存在せず、「まだ完了していない」を問い合わせる口は無い (票 C4 §2)。
//! 未完成の ID で呼ぶと WM の保持 ID と一致せず `ERR_STALE` が返るが、これを
//! 「未完成」の合図に使ってはいけない。古い gshell では `ERR_NOSYS`。

use crate::client::{GuiErr, GuiResult};
use crate::shcall;
use os32api::gui::proto::{GUI_MODAL_FILE_OPEN, GUI_MODAL_INPUT};
use os32api::gui::stub as sh;

/// `GuiString` に載る最大バイト数。
pub const MODAL_VALUE_MAX: usize = 255;

/// [`modal_result`] の戻り値。
#[derive(Clone, Copy)]
pub struct ModalResult {
    /// `GUI_MODAL_RESULT_OK` / `GUI_MODAL_RESULT_CANCEL` (契約 M1)。
    pub result: i16,
    /// WM が持っていた値の**本来の**バイト数 (最大 255)。`copied < len` なら切れた。
    pub len: usize,
    /// 実際に `out` へ書いたバイト数 (UTF-8 境界で切る)。
    pub copied: usize,
}

/// モーダルを 1 枚立てる (契約 U4)。戻り値は DialogId。**待たない**。
pub fn modal_open(parent: u32, kind: u16, message: &[u8]) -> GuiResult<u16> {
    let r = shcall!(
        sh::E_MODAL_OPEN,
        extern "C" fn(u32, u32, *const u8, u32) -> i32,
        parent,
        kind as u32,
        message.as_ptr(),
        message.len() as u32
    );
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(r as u16)
    }
}

/// File Open ダイアログを開く (契約 M1)。パスは同期的に返らない。
pub fn file_open(parent: u32, prompt: &[u8]) -> GuiResult<u16> {
    modal_open(parent, GUI_MODAL_FILE_OPEN, prompt)
}

/// 1 行入力ダイアログを開く (契約 M4)。入力文字列は同期的に返らない。
pub fn input_open(parent: u32, prompt: &[u8]) -> GuiResult<u16> {
    modal_open(parent, GUI_MODAL_INPUT, prompt)
}

/// 完了したモーダルの結果を 1 回だけ取り出す (契約 M1 / M2)。
///
/// `out` へ値 (MessageBox なら空、File Open は絶対パス、Input は UTF-8) を写す。
/// 二重 consume / ID 不一致は `ERR_STALE`、古い gshell は `ERR_NOSYS`。
pub fn modal_result(dialog: u16, out: &mut [u8]) -> GuiResult<ModalResult> {
    let mut len: u32 = 0;
    let mut copied: u32 = 0;
    let r = shcall!(
        sh::E_MODAL_RESULT,
        extern "C" fn(u32, *mut u8, u32, *mut u32, *mut u32) -> i32,
        dialog as u32,
        out.as_mut_ptr(),
        out.len() as u32,
        &mut len as *mut u32,
        &mut copied as *mut u32
    );
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(ModalResult { result: r as i16, len: len as usize, copied: copied as usize })
    }
}
