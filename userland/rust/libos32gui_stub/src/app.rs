//! app.rs — U3 のループ (契約 U3) の入口。ループ本体はライブラリ側。
//!
//! ハンドラ (`App` の実装) はアプリの関数なので、境界は C ABI の
//! [`AppVTable`](os32api::gui::stub::AppVTable) 1 枚にする。Rust の
//! トレイトオブジェクトの vtable 配置には依存しない。
//!
//! 周回の状態 (`quit` / `input_unknown` / 損傷キュー / Paint キュー) は
//! **ライブラリ側の `.bss`**。`Ui` はタイムアウト 1 個だけの取っ手で、
//! `quit()` などはジャンプ表へ落ちる。

use core::ffi::c_void;

use crate::client::{GuiErr, GuiResult};
use crate::shcall;
use os32api::gui::stub::{self as sh, AppVTable};

pub use os32api::gui::stub::{App, Ui};

/// 1 アプリ 1 本のイベントループ。`Quit` / 全ウィンドウ消滅 / `Ui::quit` で戻る。
pub fn run<A: App>(app: &mut A) -> GuiResult<()> {
    let mut ui = Ui::new();
    run_with(app, &mut ui)
}

/// `Ui` を呼び出し側が用意する版 (タイムアウトを最初から与えたいとき)。
pub fn run_with<A: App>(app: &mut A, ui: &mut Ui) -> GuiResult<()> {
    /* ハンドラ表はこの呼び出しの間だけ生きていればよい (ループは戻ってくる)。 */
    let vt: AppVTable = sh::vtable_of::<A>();
    let r = shcall!(
        sh::E_RUN,
        extern "C" fn(*const AppVTable, *mut c_void, *mut Ui) -> i32,
        &vt as *const AppVTable,
        app as *mut A as *mut c_void,
        ui as *mut Ui
    );
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(())
    }
}

/// 溜まった損傷を `OP_INVALIDATE` で送る (ふつうはループがやる)。
pub fn flush_damage() {
    shcall!(sh::E_FLUSH_DAMAGE, extern "C" fn())
}
