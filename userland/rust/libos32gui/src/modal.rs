//! modal.rs — 標準ダイアログのクライアント側 (票 C4、契約 V12-C / V12-M)。
//!
//! v1.1 の `GUI_OP_MODAL_OPEN` (op 64) はダイアログを立てるだけで、完了は
//! `GUI_EV_MODAL` (= `App::on_modal`) で受ける。v1.2 はそこに
//! `GUI_OP_MODAL_RESULT` (op 65) を足し、**File Open が選んだ絶対パス**や
//! **Input に入力された UTF-8** を受け取れるようにする。
//!
//! # 鉄則
//!
//! - **入れ子ループを作らない**。`file_open` / `input_open` は DialogId を返すだけで、
//!   パスや文字列を同期的に返さない (契約 V12-C)。
//! - [`modal_result`] は **`on_modal` を受けてから呼ぶ API**。`OS32_ERR_AGAIN` は
//!   プロトコルに存在せず、「まだ完了していない」を問い合わせる口は無い
//!   (契約 V12-C / 票 C4 §2)。未完成の ID で呼ぶと WM の保持している ID と
//!   一致しないので `ERR_STALE` が返るが、**これを「未完成」の合図に使わない**。
//! - 結果は WM が GUI スロットごとに 1 件だけ持ち、1 回の `MODAL_RESULT` で
//!   consume される。二重 consume と ID 不一致はどちらも `ERR_STALE` (契約 M2)。
//! - 古い gshell は op 65 を知らないので `OS32_ERR_NOSYS` が返る。ここは
//!   そのまま呼び出し側へ返す (panic も loop もしない。契約 V12-C の stale 規則)。

use crate::client::{self, GuiErr, GuiResult};
use os32api::gui::proto::{
    GuiReqModal, GuiReqModalResult, GuiRespModalResult, GUI_MODAL_FILE_OPEN, GUI_MODAL_INPUT,
    GUI_OP_MODAL_OPEN, GUI_OP_MODAL_RESULT,
};

/// `GuiString` に載る最大バイト数。
pub const MODAL_VALUE_MAX: usize = 255;

/// [`modal_result`] の戻り値。
#[derive(Clone, Copy)]
pub struct ModalResult {
    /// `GUI_MODAL_RESULT_OK` / `GUI_MODAL_RESULT_CANCEL` (契約 M1)。
    pub result: i16,
    /// WM が持っていた値の**本来の**バイト数 (最大 255)。
    /// `out` に収まらなかったときは `copied < len` になる。
    pub len: usize,
    /// 実際に `out` へ書いたバイト数 (UTF-8 境界で切る)。
    pub copied: usize,
}

/* ================================================================ */
/*  open (op 64) — 立てるだけ。blocking wait はしない                 */
/* ================================================================ */

/// モーダルを 1 枚立てる (契約 U4)。戻り値は DialogId (1..0x7FFF)。
///
/// `parent` は親ウィンドウ (0 = このアプリの最前面窓)。`kind` は
/// `GUI_MODAL_OK` / `GUI_MODAL_OK_CANCEL` / `GUI_MODAL_YES_NO` /
/// `GUI_MODAL_FILE_OPEN` / `GUI_MODAL_INPUT`。
///
/// **待たない**。完了は `App::on_modal(dialog, result)` で受け、値が要るなら
/// そのハンドラの中で [`modal_result`] を呼ぶ。
pub fn modal_open(parent: u32, kind: u16, message: &[u8]) -> GuiResult<u16> {
    let req = GuiReqModal { buttons: kind, _pad: 0, message: client::gui_string(message) };
    client::write_req(&req);
    let r = client::call(GUI_OP_MODAL_OPEN, parent)?;
    if r <= 0 || r > 0x7FFF {
        /* WM は DialogId を正で返す。0 や範囲外は約束違反なので STALE 扱い。 */
        return Err(GuiErr::STALE);
    }
    Ok(r as u16)
}

/// File Open ダイアログを開く (契約 M1)。戻り値は DialogId。
///
/// 選んだ絶対パスは同期的に返らない。`on_modal` の中で [`modal_result`] を呼ぶ。
pub fn file_open(parent: u32, prompt: &[u8]) -> GuiResult<u16> {
    modal_open(parent, GUI_MODAL_FILE_OPEN, prompt)
}

/// 1 行入力ダイアログを開く (契約 M4)。戻り値は DialogId。
///
/// prompt は `GuiReqModal.message` に載る。入力された UTF-8 は
/// `on_modal` の中で [`modal_result`] から取る。
pub fn input_open(parent: u32, prompt: &[u8]) -> GuiResult<u16> {
    modal_open(parent, GUI_MODAL_INPUT, prompt)
}

/* ================================================================ */
/*  result (op 65) — 1 回だけ。consume される                         */
/* ================================================================ */

/// 完了したモーダルの結果を 1 回だけ取り出す (契約 M1 / M2)。
///
/// `out` へ値 (MessageBox なら空、File Open は絶対パス、Input は UTF-8) を写す。
/// **`App::on_modal` を受けた後にだけ呼ぶこと** — 未完成状態の問い合わせ口は
/// プロトコルに無い (`OS32_ERR_AGAIN` は存在しない)。
///
/// エラー:
/// - `ERR_STALE`: ID 不一致 / 既に consume 済み
/// - `ERR_NOSYS`: op 65 を知らない古い gshell
pub fn modal_result(dialog: u16, out: &mut [u8]) -> GuiResult<ModalResult> {
    let req = GuiReqModalResult { dialog, _pad: 0 };
    client::write_req(&req);
    /* 古い WM はここで OS32_ERR_NOSYS を返す (応答ブロックは書かれない)。 */
    client::call(GUI_OP_MODAL_RESULT, dialog as u32)?;
    let resp = client::read_resp::<GuiRespModalResult>();
    if resp.result < 0 {
        return Err(GuiErr(resp.result as i32));
    }
    if resp.dialog != dialog {
        return Err(GuiErr::STALE);
    }
    let len = resp.value.len as usize;
    let len = if len > MODAL_VALUE_MAX { MODAL_VALUE_MAX } else { len };
    /* out が短いときは UTF-8 の切れ目で止める (途中の符号単位で切らない)。 */
    let copied = client::utf8_truncate(&resp.value.s[..len], out.len());
    let mut i = 0;
    while i < copied {
        out[i] = resp.value.s[i];
        i += 1;
    }
    Ok(ModalResult { result: resp.result, len, copied })
}
