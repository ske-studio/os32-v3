//! libos32gui — GUI シェル v1.1 のクライアントライブラリ (Rust, no_std)
//!
//! 契約 [`docs/tasks/gui/API_CONTRACTS.md`] の G / T / U をアプリ側から見た形で
//! 実装する。**WM (ウィンドウ管理・Z 順・フォーカス・入力取り込み) は gshell 側**
//! (`userland/gshell`) にあり、ここには無い。旧版にあった「アプリ 1 本の中で WM を
//! 動かす」API (`gui_pump` / `gui_draw` / `Window` 配列) は票 C2 で削除した。
//!
//! ```text
//!  G (描画)     draw / clip / surface   ← 票 C1。libos32gfx を FFI で呼ぶ
//!  T (呼び出し) client                  ← gui_call の薄い包み。SHM スロット
//!  U (窓と木)   window / widget / layout / timer / app
//! ```
//!
//! 鉄則:
//! - `#![no_std]`、alloc 不使用。状態はすべて固定長配列 (v2 CONTRACTS C8)。
//! - 外部クレート禁止。依存は in-repo の `os32api` だけ。
//! - プリミティブごとに syscall しない。1 周の `gui_call` は
//!   `POLL` + `COMMIT` + `WAIT` (+ 状態変更) だけ (契約 P)。
//! - ポインタを SHM とイベントに載せない。文字列は長さ前置で値渡し。
//! - 入れ子ループ禁止。モーダル (W2) の完了は `Modal` イベントで受ける。
#![no_std]

extern crate os32api;

/* ---- G 描画レイヤ (票 C1) ---- */
mod ffi;
mod gstate;
pub mod clip;
pub mod draw;
pub mod surface;

/* ---- T / U クライアント層 (票 C2) ---- */
pub mod app;
pub mod client;
pub mod layout;
pub mod timer;
mod uistate;
pub mod widget;
pub mod window;

/* ---- v1.2 デスクトップ client API (票 C4、契約 V12-C / V12-I / V12-S) ---- */
pub mod icon;
pub mod modal;
pub mod session;

/* ---- v1.3 設定レジストリ (票 S2 §3) ----
 * 表 101..=104。open〜close を 1 呼び出しで閉じるので、アプリ側に `CfgDb` は
 * 出さない。実体は C の libos32cfg.a。 */
/// `libos32cfg` の ABI 宣言 (票 S4 §4 で `os32api::cfg` へ集約した)。
///
/// `cfgro.rs` はこの**別名**だけを見る — os32api を名指ししないので、
/// ホスト TDD (`host_tests`) は同じ名前で `sdk/rust/os32api/src/cfg.rs` を
/// `#[path]` 取り込みするだけで `cfgro.rs` をそのまま組める。
pub use os32api::cfg as cfgabi;
pub mod cfgro;

/* ---- UTF-8 境界計算 (票 N4 §5 で client.rs から切り出し) ----
 * os32api にもクレートの他の部分にも依存しない。client.rs と hostsvc.rs が
 * 共用し、host_tests が `#[path]` で直に取り込む。 */
pub mod utf8core;

/* ---- 桁と折り返し (票 TASK_EDIT_GUI §2 決裁 A1) ----
 * 「3 バイトで 2 桁」を扱う下請け。`WK_TEXTBOX` / `WK_TEXTAREA` / エディタ本文が
 * **同じ実体**を使う (libos32gui_stub が `#[path]` でこのファイルを取り込む)。
 * utf8core と同じく core だけに依存し、ホスト TDD が直に取り込める。 */
pub mod textcore;

/* ---- v1.4 Host Services (票 N4 §1) ----
 * 表 105..=110。libos32host.a を呼ぶ薄い extern "C" ラッパー。`kapi` は
 * cfgro.rs の static を共用する (ここで再定義しない)。 */
/// `libos32host` の ABI 宣言 (票 N4 §1 で `os32api::host` へ集約)。
/// `hostsvc.rs` はこの**別名**だけを見る (os32api を名指ししない)。
pub use os32api::host as hostabi;
pub mod hostsvc;
/// print_file のファイル I/O 継ぎ目の本体 (os32api の sys_open/read/close)。
mod hostsvc_file;

/* ---- 共有ライブラリの先頭ページ (票 C3) ----
 * `.shlib_hdr` に 32B ヘッダ + ジャンプ表を置き、公開関数を `extern "C"` で
 * 出す。アプリはこの表を通してだけライブラリに入る (`libos32gui_stub`)。 */
pub mod shlib;

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

/* ---- よく使うものを直下へ ---- */
/* アプリは `libos32gui_stub` (同名で再公開) を使う。ここは本体 (shlib) 側。 */
pub use app::{run_vt, App, Ui};
pub use client::{GuiErr, GuiResult};
pub use layout::SizeSpec;
pub use timer::Timer;
pub use widget::{WidgetEvent, WidgetId};
pub use window::{Window, WindowSpec};

/// WM に接続する (契約 T2a の `OP_INIT`)。アプリの最初の 1 回。
///
/// `api` は crt0 が渡す `KernelAPI`。`ERR_VERSION` なら理由を出して即戻る
/// (呼び出し側が exit する)。
pub fn init(api: *mut os32api::KernelAPI) -> GuiResult<u32> {
    if api.is_null() {
        return Err(GuiErr::INVAL);
    }
    os32api::os32_init(api);
    match client::init() {
        Ok(slot) => Ok(slot),
        Err(e) => {
            client::dbg_print_num(b"[gui] OP_INIT failed:", e.code());
            client::dbg_print(e.name());
            Err(e)
        }
    }
}
