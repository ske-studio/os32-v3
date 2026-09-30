//! cfg.rs — 設定レジストリ (`/etc/settings.db`) のスタブ (票 S2 §3)。
//!
//! アプリは DB を自分で開かない。**設定の読み書きは OS 経由** (決裁
//! 2026-09-13) で、ここが叩く表のエントリが 1 呼び出しの中で
//! `cfg_open` → 読み書き → `cfg_close` まで閉じる。`CfgDb` は出てこない。
//!
//! - 読み (`get_int` / `get_text`) は全 scope。
//! - 書き (`set_int` / `set_text`) は **`app:[a-z0-9_]+` scope だけ**。
//!   `system` / `gshell` / `user` への書き込みは拒否される (S4 の設定 UI と
//!   `cfg` コマンドの領分)。
//!
//! **限界**: 「自分の `app:` scope にしか書けない」束縛は無い (アプリが自分の
//! ID を知る KAPI が無いため)。S4 / S5 で詰める。

use crate::client::{ok0, GuiErr, GuiResult};
use crate::shcall;
use os32api::gui::stub as sh;

/// scope / key の最大バイト数 (NUL を除く)。
pub const CFG_NAME_MAX: usize = 63;
/// text 値の最大バイト数 (NUL を除く)。
pub const CFG_TEXT_MAX: usize = 255;

/// 整数の設定を読む。**失敗はすべて `def`** (DB が無い / 壊れている / 未設定 /
/// close 失敗)。負の設定値も正当なので、戻り値で失敗を区別しない。
pub fn get_int(scope: &[u8], key: &[u8], def: i32) -> i32 {
    shcall!(
        sh::E_CFG_GET_INT,
        extern "C" fn(*const u8, u32, *const u8, u32, i32) -> i32,
        scope.as_ptr(),
        scope.len() as u32,
        key.as_ptr(),
        key.len() as u32,
        def
    )
}

/// 文字列の設定を `out` へ読む。戻りは写した長さ (NUL を除く)。
///
/// - `Err(NOTFOUND)` … 未設定 (呼び手が既定値を使う)
/// - `Err(NOSPC)` … `out` が足りない (`out` は書き換えない)
/// - `Err(IO)` … 閉じるのに失敗した
///
/// `out` には NUL 終端の余地が要る (`cap` は NUL 込みの大きさ)。
pub fn get_text(scope: &[u8], key: &[u8], out: &mut [u8]) -> GuiResult<usize> {
    let r = shcall!(
        sh::E_CFG_GET_TEXT,
        extern "C" fn(*const u8, u32, *const u8, u32, *mut u8, u32) -> i32,
        scope.as_ptr(),
        scope.len() as u32,
        key.as_ptr(),
        key.len() as u32,
        out.as_mut_ptr(),
        out.len() as u32
    );
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(r as usize)
    }
}

/// 整数の設定を書く。`scope` は `app:[a-z0-9_]+` でなければ拒否される。
pub fn set_int(scope: &[u8], key: &[u8], v: i32) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_CFG_SET_INT,
        extern "C" fn(*const u8, u32, *const u8, u32, i32) -> i32,
        scope.as_ptr(),
        scope.len() as u32,
        key.as_ptr(),
        key.len() as u32,
        v
    ))
}

/// 文字列の設定を書く (最大 [`CFG_TEXT_MAX`] バイト)。空値も書ける。
pub fn set_text(scope: &[u8], key: &[u8], s: &[u8]) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_CFG_SET_TEXT,
        extern "C" fn(*const u8, u32, *const u8, u32, *const u8, u32) -> i32,
        scope.as_ptr(),
        scope.len() as u32,
        key.as_ptr(),
        key.len() as u32,
        s.as_ptr(),
        s.len() as u32
    ))
}
