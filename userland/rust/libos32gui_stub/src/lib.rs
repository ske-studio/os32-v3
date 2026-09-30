//! libos32gui_stub — `libos32gui.shlib` のアプリ側スタブ (票 C3)。
//!
//! 公開する形は C2 の `libos32gui` とまったく同じ。アプリは Cargo の
//! `package =` 改名で取り込むので **ソースは無変更**:
//!
//! ```toml
//! libos32gui = { path = "../libos32gui_stub", package = "libos32gui_stub" }
//! ```
//!
//! 中身は「`MEM_SHLIB_BASE` (0x400000) の先頭ページを照合して `entry[i]` を
//! 呼ぶ」だけ。描画・ウィジェット木・U3 ループの**本体もその状態も**
//! ライブラリ側にあり、ここには残らない (アプリの .bin から消える)。
//!
//! 版照合 (`os32api::gui::stub::bind`) は最初のエントリ呼び出しで走る。
//! `magic` / `version` (= `GUI_PROTO_VERSION`) / `nfunc` のどれかが合わなければ
//! `dbg_print` して `sys_exit` する — **黙って別の関数へ飛ばない**。
#![no_std]

extern crate os32api;

pub mod app;
pub mod client;
pub mod clip;
pub mod draw;
pub mod layout;
pub mod surface;
pub mod timer;
pub mod widget;
pub mod window;

/* ---- 桁と折り返し (票 TASK_EDIT_GUI §2 決裁 A1) ----
 * ジャンプ表は通さない。**純粋な計算**なのでアプリに直接載せる。
 * 実体は共有ライブラリ側の 1 ファイルで、`#[path]` で同じソースを読む —
 * 写しを作ると「3 バイトで 2 桁」の数え方がずれ、日本語で必ず露見する
 * (`docs/POLICY_DEBUG.md` §4-27)。 */
#[path = "../../libos32gui/src/textcore.rs"]
pub mod textcore;

/* ---- v1.2 デスクトップ client API (票 C4、契約 V12-C / V12-I / V12-S) ---- */
pub mod icon;
pub mod modal;
pub mod session;

/* ---- v1.3 設定レジストリ (票 S2 §3)。表 101..=104 ---- */
pub mod cfg;

/* ---- v1.4 Host Services (票 N4 §1)。表 105..=110 ----
 * ファイラ印刷・端末コピペ (N4b) はこの 6 本だけを使う。 */
pub mod host;

/// ジャンプ表のエントリを取り出して呼ぶ。
///
/// `$idx` は `os32api::gui::stub::E_*`、`$ty` はそのエントリの
/// `extern "C"` シグネチャ (ライブラリ側 `shlib.rs` と一致していること)。
macro_rules! shcall {
    ($idx:expr, $ty:ty $(, $a:expr)* $(,)?) => {
        (unsafe { ::os32api::gui::stub::fp::<$ty>($idx) })($($a),*)
    };
}
pub(crate) use shcall;

/// 契約 G の一式をまとめた入口 (`libos32gui::gapi::*`)。
pub mod gapi {
    /// 共有定数 (op / イベント種別 / Style フラグ / 色 / 上限 / SHM / エラー)。
    pub use os32api::gui::proto;
    /// 基本型 (Rect / Color / Style / SurfaceId / BitmapId / FontId / ScreenInfo / Stats)。
    pub use os32api::gui::types;

    pub use crate::clip::{clear_base_clip, current_clip, pop_clip, push_clip, set_base_clip};
    pub use crate::draw::{
        base_violation_count, blit, draw_rect, fill_rect, hline, line, measure_text, screen_info,
        stats, text, vline,
    };
    pub use crate::surface::{
        create_surface, create_window_surface, destroy_surface, screen_surface, surface_size,
    };
    /// 16x16 標準アイコン (契約 V12-I、票 C4)。
    pub use crate::icon::{draw_icon16, GuiIcon16, ICON16_SIZE};
}

/* ---- よく使うものを直下へ (アプリの `use libos32gui::*;` 用) ---- */
pub use app::{run, run_with, App, Ui};
pub use client::{GuiErr, GuiResult};
pub use icon::{draw_icon16, GuiIcon16};
pub use layout::SizeSpec;
pub use modal::{file_open, input_open, modal_open, modal_result, ModalResult};
pub use session::{session_launch, session_request, session_shutdown, session_switch_cui};
pub use timer::Timer;
pub use widget::{WidgetEvent, WidgetId};
pub use window::{Window, WindowSpec};

/// WM に接続する (契約 T2a の `OP_INIT`)。アプリの最初の 1 回。
///
/// `api` は crt0 が渡す `KernelAPI`。ここで
///   1. `os32_init(api)` (アプリ側の KAPI 実体)
///   2. `bind()` — 0x400000 の `magic` / `version` 照合 → `shlib_init(api)`
///      (ライブラリ側の KAPI 実体と libos32gfx のアタッチ)
///   3. `OP_INIT`
/// の順に進む。`ERR_VERSION` なら理由を出して即戻る (呼び出し側が exit する)。
pub fn init(api: *mut os32api::KernelAPI) -> GuiResult<u32> {
    if api.is_null() {
        return Err(GuiErr::INVAL);
    }
    os32api::os32_init(api);
    os32api::gui::stub::bind();
    let r = shcall!(os32api::gui::stub::E_CLIENT_INIT, extern "C" fn() -> i32);
    if r < 0 {
        let e = GuiErr(r);
        client::dbg_print_num(b"[gui] OP_INIT failed:", r);
        client::dbg_print(e.name());
        return Err(e);
    }
    Ok(r as u32)
}
