//! session.rs — gshell へのセッション要求 (票 C4、契約 V12-S の S2 / S3 / S4)。
//!
//! 外部アプリは `exec_run()` を自分で呼ばない。別アプリの起動・CUI 復帰・
//! system halt は `GUI_OP_SESSION_REQUEST` (op 66) で gshell へ**依頼**し、
//! gshell が `Quit{reason}` を投げ、現アプリが終了して top-level へ戻ってから
//! 実行する。
//!
//! # 戻り値の意味
//!
//! `Ok(())` は **pending として受理された**だけで、action の完了ではない
//! (契約 S4)。成功した場合ふつうは直後に `GUI_EV_QUIT` が届き、U3 ループが
//! `App::on_quit` (既定は `ui.quit()`) を通って戻る。完了 callback は
//! v1.2 では作らない (票 C4 §5)。
//!
//! エラー:
//! - `ERR_INVAL`: action / flags / path が不正 (**path 検査はここで先に行い、
//!   不正なら WM を呼ばずに返す**)
//! - `ERR_FULL`: 既に別の SessionAction が pending
//! - `ERR_NOSYS`: op 66 を知らない古い gshell (そのまま呼び出し側へ返す)

use crate::client::{self, GuiErr, GuiResult};
use os32api::gui::proto::{
    GuiReqSession, GuiString, GUI_OP_SESSION_REQUEST, GUI_SESSION_LAUNCH, GUI_SESSION_SHUTDOWN,
    GUI_SESSION_SWITCH_CUI,
};

/// `LAUNCH` の path に載る最大バイト数 (契約 S4)。
pub const SESSION_PATH_MAX: usize = 255;

/// NUL 終端も尊重した実効長 (`GuiString` に載せられる範囲)。
fn eff_len(path: &[u8]) -> usize {
    let mut n = 0usize;
    while n < path.len() && n <= SESSION_PATH_MAX {
        if path[n] == 0 {
            break;
        }
        n += 1;
    }
    n
}

/* ================================================================ */
/*  低レベル (op 66)                                                 */
/* ================================================================ */

/// SessionAction を 1 件依頼する (契約 S4)。ふつうは下の 3 つを使う。
///
/// `action` は `GUI_SESSION_LAUNCH` / `GUI_SESSION_SWITCH_CUI` /
/// `GUI_SESSION_SHUTDOWN`。`value` は LAUNCH のときだけ 1〜255B の絶対パス、
/// 他は空でなければならない。不正な組合せは **WM を呼ばずに** `ERR_INVAL`。
pub fn session_request(action: u8, value: &[u8]) -> GuiResult<()> {
    let n = eff_len(value);
    match action {
        GUI_SESSION_LAUNCH => {
            /* 1〜255B の絶対パスだけ (契約 S4)。 */
            if n == 0 || n > SESSION_PATH_MAX || value[0] != b'/' {
                return Err(GuiErr::INVAL);
            }
        }
        GUI_SESSION_SWITCH_CUI | GUI_SESSION_SHUTDOWN => {
            if n != 0 {
                return Err(GuiErr::INVAL);
            }
        }
        _ => return Err(GuiErr::INVAL),
    }

    let mut s = GuiString { len: n as u8, s: [0u8; 255] };
    let mut i = 0;
    while i < n {
        s.s[i] = value[i];
        i += 1;
    }
    /* flags は v1.2 では 0 のみ (契約 S4)。 */
    let req = GuiReqSession { action, flags: 0, _pad: 0, value: s };
    client::write_req(&req);
    /* arg は使わない (要求ブロックがすべて)。古い WM はここで ERR_NOSYS。 */
    client::call(GUI_OP_SESSION_REQUEST, 0).map(|_| ())
}

/* ================================================================ */
/*  よく使う 3 つ                                                    */
/* ================================================================ */

/// 別の GUI アプリを起動してもらう (契約 S2 / F1)。`path` は 1〜255B の絶対パス。
///
/// 受理されると gshell は `LAUNCH(path)` を pending にし、こちらへ
/// `Quit{reason=REPLACE_APP}` を投げる。**このアプリが終了してから**次が起動する。
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
