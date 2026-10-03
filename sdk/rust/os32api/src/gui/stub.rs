//! stub.rs — 共有ライブラリ `libos32gui.shlib` のジャンプ表 ABI (票 C3)。
//!
//! ここに置くのは **ライブラリ側とアプリ側の両方が同じものを見なければならない
//! 定義だけ**:
//!
//! ```text
//!   ShlibHeader   … 先頭 4KB の形 (C の OS32ShlibHeader と同一レイアウト)
//!   E_* / NFUNC   … ジャンプ表の番号 (**末尾追記のみ**)
//!   Ui / App / AppVTable … U3 ループとハンドラの境界 (C ABI)
//!   WidgetId      … App のハンドラ引数に出るのでここに置く
//!   bind() / fp() … 版照合つきの解決
//! ```
//!
//! **番号表の正典は `userland/rust/libos32gui/src/shlib.rs` のコメント表**。
//! ここの `E_*` はその写しで、`tools/mkshlib.py` が両者の順序を突き合わせる。
//!
//! 鉄則 (票 C3):
//! - 位置依存。ライブラリは [`MEM_SHLIB_BASE`] に常駐する (再配置しない)。
//! - 版が合わなければ**起動しない**。黙って別の関数へ飛ぶ stale の罠を作らない。
//! - `entry[]` は末尾追記のみ (KAPI と同じ作法)。
#![allow(dead_code)]

use core::cell::UnsafeCell;
use core::ffi::c_void;

use super::proto::{GuiEvent, GUI_PROTO_VERSION};
use super::types::{Rect, Style, SurfaceId};
use crate::KernelAPI;

/* ================================================================ */
/*  境界で使う詰め方                                                  */
/*                                                                  */
/*  `Rect` (8B, i16×4) は `#[repr(C)]` のまま値渡しする。`Style` は    */
/*  3 バイトの半端な構造体なので、i386 cdecl の解釈に依存しないよう    */
/*  u32 に詰めて渡す。                                                */
/* ================================================================ */

/// `Style` を u32 へ詰める (`fg | bg<<8 | flags<<16`)。
#[inline]
pub const fn style_bits(s: Style) -> u32 {
    (s.fg as u32) | ((s.bg as u32) << 8) | ((s.flags as u32) << 16)
}

/// [`style_bits`] の逆。
#[inline]
pub const fn style_of(bits: u32) -> Style {
    Style {
        fg: (bits & 0xFF) as u8,
        bg: ((bits >> 8) & 0xFF) as u8,
        flags: ((bits >> 16) & 0xFF) as u8,
    }
}

/// `SizeSpec` の種別 (契約 U7)。ジャンプ表 `E_W_ADD` の引数。
pub const SIZE_FIXED: u32 = 0;
pub const SIZE_FLEX: u32 = 1;
pub const SIZE_ABSOLUTE: u32 = 2;

/* ================================================================ */
/*  常駐先と先頭ページの形                                            */
/* ================================================================ */

/// 共有ライブラリの常駐先 (正典: `include/memmap.h` の `MEM_SHLIB_BASE`、K3)。
pub const MEM_SHLIB_BASE: u32 = 0x8000_0000;

/// `'SLIB'` (リトルエンディアン)。正典: `os32_kapi_shared.h` `OS32_SHLIB_MAGIC`。
pub const OS32_SHLIB_MAGIC: u32 = 0x4249_4C53;
/// ジャンプ表を含む先頭ページ。
pub const OS32_SHLIB_HDR_SIZE: usize = 4096;
/// `entry[0]` のヘッダ先頭からのオフセット。
pub const OS32_SHLIB_ENTRY_OFF: usize = 32;
/// `entry[]` に置ける最大本数。
pub const OS32_SHLIB_MAX_FUNC: usize = (OS32_SHLIB_HDR_SIZE - OS32_SHLIB_ENTRY_OFF) / 4;

/// 先頭 32 バイト。C の `OS32ShlibHeader` と同一レイアウト。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ShlibHeader {
    /// 0x00: [`OS32_SHLIB_MAGIC`]
    pub magic: u32,
    /// 0x04: ライブラリの版 (libos32gui は `GUI_PROTO_VERSION`)
    pub version: u32,
    /// 0x08: `entry[]` の有効数
    pub nfunc: u32,
    /// 0x0C: `.data`/`.bss` の先頭仮想アドレス (ページ境界)
    pub data_vaddr: u32,
    /// 0x10: アプリごとに複製する物理ページ数
    pub data_pages: u32,
    /// 0x14: 共有する read-only ページ数 (先頭ページ含む)
    pub text_pages: u32,
    /// 0x18: 0
    pub _rsvd: [u32; 2],
    /* 0x20: u32 entry[nfunc] */
}

const _: () = assert!(core::mem::size_of::<ShlibHeader>() == OS32_SHLIB_ENTRY_OFF);

/* ================================================================ */
/*  ジャンプ表の番号 (**末尾追記のみ**)                                */
/*                                                                  */
/*  正典は libos32gui/src/shlib.rs のコメント表。順序は                */
/*  `python3 tools/mkshlib.py --check` が突き合わせる。                */
/* ================================================================ */

/* --- 0: 初期化 --- */
pub const E_SHLIB_INIT: usize = 0;

/* --- 1..=20: client (契約 T) --- */
pub const E_CLIENT_INIT: usize = 1;
pub const E_DBG_PRINT: usize = 2;
pub const E_DBG_PRINT_NUM: usize = 3;
pub const E_IS_INITED: usize = 4;
pub const E_SLOT: usize = 5;
pub const E_SLOT_BASE: usize = 6;
pub const E_RAW_CALL: usize = 7;
pub const E_ARGS_PTR: usize = 8;
pub const E_READ_HEADER: usize = 9;
pub const E_POLL: usize = 10;
pub const E_WAIT: usize = 11;
pub const E_ENTER_HANDLER: usize = 12;
pub const E_LEAVE_HANDLER: usize = 13;
pub const E_COMMIT: usize = 14;
pub const E_INVALIDATE: usize = 15;
pub const E_CLIENT_STATS: usize = 16;
pub const E_TRACE_TICK: usize = 17;
pub const E_LEASE_PALETTE: usize = 18;
pub const E_UTF8_SEQ_LEN: usize = 19;
pub const E_UTF8_TRUNCATE: usize = 20;

/* --- 21..=32: 生のウィンドウ op / タイマ (契約 U1 / U5) --- */
pub const E_WIN_CREATE: usize = 21;
pub const E_WIN_DESTROY: usize = 22;
pub const E_WIN_RAISE: usize = 23;
pub const E_WIN_SET_FOCUS: usize = 24;
pub const E_WIN_CLIENT_RECT: usize = 25;
pub const E_WIN_MOVE: usize = 26;
pub const E_WIN_RESIZE: usize = 27;
pub const E_WIN_SHOW: usize = 28;
pub const E_WIN_SET_TITLE: usize = 29;
pub const E_WIN_SET_TEXT_CURSOR: usize = 30;
pub const E_TIMER_SET: usize = 31;
pub const E_TIMER_KILL: usize = 32;

/* --- 33..=37: クリップ (契約 G2) --- */
pub const E_SET_BASE_CLIP: usize = 33;
pub const E_CLEAR_BASE_CLIP: usize = 34;
pub const E_PUSH_CLIP: usize = 35;
pub const E_POP_CLIP: usize = 36;
pub const E_CURRENT_CLIP: usize = 37;

/* --- 38..=48: 描画 (契約 G2 / G5 / G7) --- */
pub const E_FILL_RECT: usize = 38;
pub const E_DRAW_RECT: usize = 39;
pub const E_HLINE: usize = 40;
pub const E_VLINE: usize = 41;
pub const E_LINE: usize = 42;
pub const E_BLIT: usize = 43;
pub const E_TEXT: usize = 44;
pub const E_MEASURE_TEXT: usize = 45;
pub const E_SCREEN_INFO: usize = 46;
pub const E_GFX_STATS: usize = 47;
pub const E_BASE_VIOLATION_COUNT: usize = 48;

/* --- 49..=53: サーフェス (契約 G3) --- */
pub const E_CREATE_SURFACE: usize = 49;
pub const E_DESTROY_SURFACE: usize = 50;
pub const E_CREATE_WINDOW_SURFACE: usize = 51;
pub const E_SCREEN_SURFACE: usize = 52;
pub const E_SURFACE_SIZE: usize = 53;

/* --- 54..=79: ウィジェット (契約 U6) --- */
pub const E_W_ROW: usize = 54;
pub const E_W_COLUMN: usize = 55;
pub const E_W_LABEL: usize = 56;
pub const E_W_BUTTON: usize = 57;
pub const E_W_CHECKBOX: usize = 58;
pub const E_W_TEXTBOX: usize = 59;
pub const E_W_LISTBOX: usize = 60;
pub const E_W_ADD: usize = 61;
pub const E_W_SET_CROSS: usize = 62;
pub const E_W_SET_MIN: usize = 63;
pub const E_W_SET_TEXT: usize = 64;
pub const E_W_GET_TEXT: usize = 65;
pub const E_W_SET_CHECKED: usize = 66;
pub const E_W_IS_CHECKED: usize = 67;
pub const E_W_SET_ENABLED: usize = 68;
pub const E_W_SET_HIDDEN: usize = 69;
pub const E_W_RECT: usize = 70;
pub const E_W_LIST_CLEAR: usize = 71;
pub const E_W_LIST_ADD: usize = 72;
pub const E_W_LIST_SELECTION: usize = 73;
pub const E_W_LIST_SET_SELECTION: usize = 74;
pub const E_W_LIST_ITEM_TEXT: usize = 75;
pub const E_W_SET_FOCUS: usize = 76;
pub const E_W_FOCUSED: usize = 77;
pub const E_W_RESOLVE: usize = 78;
pub const E_W_ID_OF: usize = 79;

/* --- 80..=88: ウィンドウ所有型の下請け (契約 U1 / T4) --- */
pub const E_WINDOW_CREATE: usize = 80;
pub const E_WINDOW_SURFACE: usize = 81;
pub const E_WINDOW_CLIENT_SIZE: usize = 82;
pub const E_WINDOW_SET_ROOT: usize = 83;
pub const E_WINDOW_RELAYOUT: usize = 84;
pub const E_WINDOW_INVALIDATE: usize = 85;
pub const E_WINDOW_IS_FOCUSED: usize = 86;
pub const E_WINDOW_DROP: usize = 87;
pub const E_WINDOW_COUNT: usize = 88;

/* --- 89..=94: U3 ループと Ui --- */
pub const E_RUN: usize = 89;
pub const E_FLUSH_DAMAGE: usize = 90;
pub const E_UI_QUIT: usize = 91;
pub const E_UI_IS_QUITTING: usize = 92;
pub const E_UI_INPUT_UNKNOWN: usize = 93;
pub const E_UI_KEY_IS_PRESSED: usize = 94;

/* --- 95..=100: v1.2 デスクトップ client API (票 C4、契約 V12-C / V12-I) --- */
pub const E_MODAL_OPEN: usize = 95;
pub const E_MODAL_RESULT: usize = 96;
pub const E_FILE_OPEN: usize = 97;
pub const E_INPUT_OPEN: usize = 98;
pub const E_SESSION_REQUEST: usize = 99;
pub const E_DRAW_ICON16: usize = 100;

/* --- 101..=104: v1.3 設定レジストリ (票 S2 §3、決裁 2026-09-13) ---
 *
 * open〜close を wrapper の中で閉じるので `CfgDb` はアプリに出ない。
 * set は `app:[a-z0-9_]+` scope だけが通る (get は全 scope)。 */
pub const E_CFG_GET_INT: usize = 101;
pub const E_CFG_GET_TEXT: usize = 102;
pub const E_CFG_SET_INT: usize = 103;
pub const E_CFG_SET_TEXT: usize = 104;

/* --- 105..=110: v1.4 Host Services (票 N4 §1) ---
 *
 * libos32host.a を呼ぶ薄い extern "C" ラッパー。open〜close の多段・AGAIN
 * ループ・ストリーミングは C 側が隠す。エラーは `HOST_E*` をそのまま返す。 */
pub const E_HOST_GET: usize = 105;
pub const E_PRINT_TEXT: usize = 106;
pub const E_PRINT_FILE: usize = 107;
pub const E_CLIP_GET: usize = 108;
pub const E_CLIP_PUT: usize = 109;
pub const E_HOST_TIME: usize = 110;

/* --- 111..=117: v1.4 複数行編集 WK_TEXTAREA (票 TASK_EDIT_GUI §2、決裁 A1) ---
 *
 * `E_W_TEXTBOX` (59) は **1 行のまま**。複数行は新しい種別を末尾に足した
 * (API_CONTRACTS.md の「末尾追記のみ」に収まる)。
 *
 * **部品は本文を持たない。** アプリが本文を持ち、`textarea_clear` +
 * `textarea_add_row` で「いま見えている行」だけを写す。折り返しの桁は
 * `textarea_columns`、入る行数は `textarea_visible_rows` から取る ([C4])。 */
pub const E_W_TEXTAREA: usize = 111;
pub const E_W_TEXTAREA_CLEAR: usize = 112;
pub const E_W_TEXTAREA_ADD_ROW: usize = 113;
pub const E_W_TEXTAREA_SET_CARET: usize = 114;
pub const E_W_TEXTAREA_VISIBLE_ROWS: usize = 115;
pub const E_W_TEXTAREA_COLUMNS: usize = 116;
pub const E_W_TEXTAREA_TAKE_INPUT: usize = 117;

/* --- 118: 窓 (WindowId) でフォーカス中のウィジェット (穴 H13 の後始末) ---
 *
 * 既存の `E_W_FOCUSED` (77) は引数が**窓スロットの添字**で、アプリが持つのは
 * WindowId。添字を引く口が公開されていないので、**アプリからは使えなかった**。
 * そのためアプリはフォーカスを自分で追うしかなく、それが穴 H13 を踏む前提だった
 * (`docs/archive/gui_v14/TASK_EDIT_GUI.md` §9-6)。
 *
 * **77 の意味は変えない** — スロット添字を渡している呼び手 (単一窓なら 0 が
 * 偶然当たる) が黙って壊れる。末尾に足す (決裁 A1 の「末尾追記のみ」)。 */
pub const E_W_FOCUSED_IN: usize = 118;

/// ジャンプ表の本数 (末尾追記のたびに増やす)。
pub const SHLIB_NFUNC: usize = 119;

const _: () = assert!(SHLIB_NFUNC <= OS32_SHLIB_MAX_FUNC);

/* ================================================================ */
/*  Icon16 (契約 V12-I) — WM と libos32gui が同じ論理形式を使う       */
/*                                                                  */
/*  16x16 固定。`pixels` は 4bpp row-major (even x = 上位ニブル、     */
/*  odd x = 下位ニブル)、`mask` は 1bpp row-major (bit7 が左端、      */
/*  1 = 不透明 / 0 = 透明)。色 index は GUI system 16 色 (proto の    */
/*  `GUI_COLOR_*`)。拡大縮小・PNG/BMP/ICO の解読は v1.2 対象外。      */
/* ================================================================ */

/// 16x16 の標準アイコン (160B)。`draw_icon16` に渡す。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GuiIcon16 {
    /// 16*16*4bpp = 128B。`pixels[y*8 + x/2]`、even x = 上位ニブル。
    pub pixels: [u8; 128],
    /// 16*16*1bpp = 32B。`mask[y*2 + x/8]` の bit(7 - x%8)。1 = 不透明。
    pub mask: [u8; 32],
}

impl GuiIcon16 {
    /// 全画素が色 0 / 全透明のアイコン。
    pub const ZERO: GuiIcon16 = GuiIcon16 { pixels: [0; 128], mask: [0; 32] };

    /// `(x, y)` の色 index (0..15)。範囲外は 0。
    #[inline]
    pub fn pixel(&self, x: usize, y: usize) -> u8 {
        if x >= 16 || y >= 16 {
            return 0;
        }
        let b = self.pixels[y * 8 + (x >> 1)];
        if x & 1 == 0 {
            b >> 4
        } else {
            b & 0x0F
        }
    }

    /// `(x, y)` が不透明か。範囲外は false。
    #[inline]
    pub fn opaque(&self, x: usize, y: usize) -> bool {
        if x >= 16 || y >= 16 {
            return false;
        }
        (self.mask[y * 2 + (x >> 3)] >> (7 - (x & 7))) & 1 != 0
    }
}

const _: () = assert!(core::mem::size_of::<GuiIcon16>() == 160);
const _: () = assert!(core::mem::align_of::<GuiIcon16>() == 1);

/* ================================================================ */
/*  WidgetId — App のハンドラ引数に出るので共有側に置く                */
/* ================================================================ */

/// ウィジェットのハンドル (`index:16 | generation:16`、0 = 無効)。
#[repr(transparent)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct WidgetId(pub u32);

impl WidgetId {
    pub const NULL: WidgetId = WidgetId(0);

    #[inline]
    pub const fn new(index: u16, generation: u16) -> WidgetId {
        WidgetId((index as u32) | ((generation as u32) << 16))
    }
    #[inline]
    pub const fn index(self) -> u16 {
        (self.0 & 0xFFFF) as u16
    }
    #[inline]
    pub const fn generation(self) -> u16 {
        (self.0 >> 16) as u16
    }
    #[inline]
    pub const fn is_null(self) -> bool {
        self.0 == 0
    }
    #[inline]
    pub const fn raw(self) -> u32 {
        self.0
    }
}

/* ================================================================ */
/*  Ui — ループからハンドラへ渡す取っ手                               */
/*                                                                  */
/*  実体はループ (ライブラリ側) が持つ。フィールドは `timeout_ticks`   */
/*  ひとつだけで、状態を触るメソッドはジャンプ表へ落ちる。            */
/* ================================================================ */

#[repr(C)]
pub struct Ui {
    /// 次の `OP_WAIT` のタイムアウト (tick)。0 = 期限なし。
    pub timeout_ticks: u32,
}

impl Ui {
    #[inline]
    pub const fn new() -> Ui {
        Ui { timeout_ticks: 0 }
    }

    /// ループを終える。
    #[inline]
    pub fn quit(&mut self) {
        unsafe { fp::<extern "C" fn()>(E_UI_QUIT)() }
    }

    #[inline]
    pub fn is_quitting(&self) -> bool {
        unsafe { fp::<extern "C" fn() -> u32>(E_UI_IS_QUITTING)() != 0 }
    }

    /// 生きているウィンドウ枚数 (0 になったらループは戻る)。
    #[inline]
    pub fn window_count(&self) -> usize {
        unsafe { fp::<extern "C" fn() -> u32>(E_WINDOW_COUNT)() as usize }
    }

    /// `OVERFLOW` を受けた直後か (入力状態を未知として扱う。契約 T3)。
    #[inline]
    pub fn input_unknown(&self) -> bool {
        unsafe { fp::<extern "C" fn() -> u32>(E_UI_INPUT_UNKNOWN)() != 0 }
    }

    /// 押下状態を読み直す (`OVERFLOW` からの復帰。契約 T3)。
    #[inline]
    pub fn key_is_pressed(&self, scan: u8) -> bool {
        unsafe { fp::<extern "C" fn(u32) -> u32>(E_UI_KEY_IS_PRESSED)(scan as u32) != 0 }
    }
}

impl Default for Ui {
    fn default() -> Ui {
        Ui::new()
    }
}

/* ================================================================ */
/*  App — 種別ごとのハンドラ (契約 U6)                                */
/*                                                                  */
/*  トレイトはアプリ側で実装される。ループはライブラリ側にあるので、   */
/*  境界は `AppVTable` (C ABI) にする。Rust のトレイトオブジェクトの    */
/*  vtable 配置には依存しない。                                       */
/* ================================================================ */

#[allow(unused_variables)]
pub trait App {
    /* --- ウィジェット (クライアント側で合成) --- */
    fn on_click(&mut self, ui: &mut Ui, w: WidgetId) {}
    fn on_toggled(&mut self, ui: &mut Ui, w: WidgetId, on: bool) {}
    fn on_text_changed(&mut self, ui: &mut Ui, w: WidgetId) {}
    fn on_select(&mut self, ui: &mut Ui, w: WidgetId, index: i32) {}
    fn on_widget_focus(&mut self, ui: &mut Ui, w: WidgetId) {}

    /* --- WM 由来 --- */
    /// 生イベント (種別ごとのハンドラの前に 1 回)。`serial` を見たい計測器
    /// (`gui_bench`、契約 P2) 用。ふつうのアプリは実装しない。
    fn on_raw(&mut self, ui: &mut Ui, ev: &GuiEvent) {}

    /// 閉じるボタン。既定はループ終了 (破棄はアプリが決める。契約 U1)。
    fn on_close(&mut self, ui: &mut Ui, window: u32) {
        ui.quit();
    }
    fn on_focus(&mut self, ui: &mut Ui, window: u32, focused: bool) {}
    /// 生キー (契約 U2a: `scan` は PC-98 スキャンコード)。`down=false` も来る。
    fn on_key(&mut self, ui: &mut Ui, window: u32, scan: u8, ch: u8, mods: u8, down: bool) {}
    fn on_timer(&mut self, ui: &mut Ui, window: u32, id: u8) {}
    fn on_configure(&mut self, ui: &mut Ui, window: u32) {}
    fn on_palette(&mut self, ui: &mut Ui, window: u32, active: bool) {}
    fn on_modal(&mut self, ui: &mut Ui, dialog: u16, result: i16) {}
    /// WM からの終了要求。既定はループ終了。
    fn on_quit(&mut self, ui: &mut Ui, reason: u8) {
        ui.quit();
    }
    /// 取りこぼし通知 (契約 T3)。押しっぱなしの前提を捨てる。
    fn on_overflow(&mut self, ui: &mut Ui, dropped: u16) {}

    /* --- 描画 --- */
    /// ウィジェット木の後に呼ばれる。基底クリップは `rect` に固定済み (契約 G2)。
    fn on_paint(&mut self, ui: &mut Ui, window: u32, surface: SurfaceId, rect: Rect) {}

    /// `commit` の直後 (`OP_WAIT` の前)。計測 (`gui_bench`) 用の締めの場所。
    fn after_commit(&mut self, ui: &mut Ui) {}
}

/// ループ (ライブラリ) → ハンドラ (アプリ) の呼び出し表。**末尾追記のみ**。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct AppVTable {
    pub on_click: extern "C" fn(*mut c_void, *mut Ui, u32),
    pub on_toggled: extern "C" fn(*mut c_void, *mut Ui, u32, u32),
    pub on_text_changed: extern "C" fn(*mut c_void, *mut Ui, u32),
    pub on_select: extern "C" fn(*mut c_void, *mut Ui, u32, i32),
    pub on_widget_focus: extern "C" fn(*mut c_void, *mut Ui, u32),
    pub on_raw: extern "C" fn(*mut c_void, *mut Ui, *const GuiEvent),
    pub on_close: extern "C" fn(*mut c_void, *mut Ui, u32),
    pub on_focus: extern "C" fn(*mut c_void, *mut Ui, u32, u32),
    pub on_key: extern "C" fn(*mut c_void, *mut Ui, u32, u32, u32, u32, u32),
    pub on_timer: extern "C" fn(*mut c_void, *mut Ui, u32, u32),
    pub on_configure: extern "C" fn(*mut c_void, *mut Ui, u32),
    pub on_palette: extern "C" fn(*mut c_void, *mut Ui, u32, u32),
    pub on_modal: extern "C" fn(*mut c_void, *mut Ui, u32, i32),
    pub on_quit: extern "C" fn(*mut c_void, *mut Ui, u32),
    pub on_overflow: extern "C" fn(*mut c_void, *mut Ui, u32),
    pub on_paint: extern "C" fn(*mut c_void, *mut Ui, u32, u32, Rect),
    pub after_commit: extern "C" fn(*mut c_void, *mut Ui),
}

/* ---- 型消去した薄いサンク (アプリ側でだけ実体化される) ---- */

extern "C" fn t_click<A: App>(p: *mut c_void, ui: *mut Ui, w: u32) {
    unsafe { (*(p as *mut A)).on_click(&mut *ui, WidgetId(w)) }
}
extern "C" fn t_toggled<A: App>(p: *mut c_void, ui: *mut Ui, w: u32, on: u32) {
    unsafe { (*(p as *mut A)).on_toggled(&mut *ui, WidgetId(w), on != 0) }
}
extern "C" fn t_text_changed<A: App>(p: *mut c_void, ui: *mut Ui, w: u32) {
    unsafe { (*(p as *mut A)).on_text_changed(&mut *ui, WidgetId(w)) }
}
extern "C" fn t_select<A: App>(p: *mut c_void, ui: *mut Ui, w: u32, index: i32) {
    unsafe { (*(p as *mut A)).on_select(&mut *ui, WidgetId(w), index) }
}
extern "C" fn t_widget_focus<A: App>(p: *mut c_void, ui: *mut Ui, w: u32) {
    unsafe { (*(p as *mut A)).on_widget_focus(&mut *ui, WidgetId(w)) }
}
extern "C" fn t_raw<A: App>(p: *mut c_void, ui: *mut Ui, ev: *const GuiEvent) {
    unsafe { (*(p as *mut A)).on_raw(&mut *ui, &*ev) }
}
extern "C" fn t_close<A: App>(p: *mut c_void, ui: *mut Ui, window: u32) {
    unsafe { (*(p as *mut A)).on_close(&mut *ui, window) }
}
extern "C" fn t_focus<A: App>(p: *mut c_void, ui: *mut Ui, window: u32, focused: u32) {
    unsafe { (*(p as *mut A)).on_focus(&mut *ui, window, focused != 0) }
}
extern "C" fn t_key<A: App>(
    p: *mut c_void,
    ui: *mut Ui,
    window: u32,
    scan: u32,
    ch: u32,
    mods: u32,
    down: u32,
) {
    unsafe {
        (*(p as *mut A)).on_key(&mut *ui, window, scan as u8, ch as u8, mods as u8, down != 0)
    }
}
extern "C" fn t_timer<A: App>(p: *mut c_void, ui: *mut Ui, window: u32, id: u32) {
    unsafe { (*(p as *mut A)).on_timer(&mut *ui, window, id as u8) }
}
extern "C" fn t_configure<A: App>(p: *mut c_void, ui: *mut Ui, window: u32) {
    unsafe { (*(p as *mut A)).on_configure(&mut *ui, window) }
}
extern "C" fn t_palette<A: App>(p: *mut c_void, ui: *mut Ui, window: u32, active: u32) {
    unsafe { (*(p as *mut A)).on_palette(&mut *ui, window, active != 0) }
}
extern "C" fn t_modal<A: App>(p: *mut c_void, ui: *mut Ui, dialog: u32, result: i32) {
    unsafe { (*(p as *mut A)).on_modal(&mut *ui, dialog as u16, result as i16) }
}
extern "C" fn t_quit<A: App>(p: *mut c_void, ui: *mut Ui, reason: u32) {
    unsafe { (*(p as *mut A)).on_quit(&mut *ui, reason as u8) }
}
extern "C" fn t_overflow<A: App>(p: *mut c_void, ui: *mut Ui, dropped: u32) {
    unsafe { (*(p as *mut A)).on_overflow(&mut *ui, dropped as u16) }
}
extern "C" fn t_paint<A: App>(
    p: *mut c_void,
    ui: *mut Ui,
    window: u32,
    surface: u32,
    rect: Rect,
) {
    unsafe { (*(p as *mut A)).on_paint(&mut *ui, window, SurfaceId(surface), rect) }
}
extern "C" fn t_after_commit<A: App>(p: *mut c_void, ui: *mut Ui) {
    unsafe { (*(p as *mut A)).after_commit(&mut *ui) }
}

/// 具体型 `A` のハンドラ表を組む (アプリ側で 1 回だけ)。
pub const fn vtable_of<A: App>() -> AppVTable {
    AppVTable {
        on_click: t_click::<A>,
        on_toggled: t_toggled::<A>,
        on_text_changed: t_text_changed::<A>,
        on_select: t_select::<A>,
        on_widget_focus: t_widget_focus::<A>,
        on_raw: t_raw::<A>,
        on_close: t_close::<A>,
        on_focus: t_focus::<A>,
        on_key: t_key::<A>,
        on_timer: t_timer::<A>,
        on_configure: t_configure::<A>,
        on_palette: t_palette::<A>,
        on_modal: t_modal::<A>,
        on_quit: t_quit::<A>,
        on_overflow: t_overflow::<A>,
        on_paint: t_paint::<A>,
        after_commit: t_after_commit::<A>,
    }
}

/* ================================================================ */
/*  解決 (版照合 → ジャンプ表)                                        */
/* ================================================================ */

struct Table(UnsafeCell<*const u32>);
unsafe impl Sync for Table {}
static TABLE: Table = Table(UnsafeCell::new(core::ptr::null()));

/// 版が合わないときの停止 (契約 C3 の鉄則: **起動しない**)。
///
/// KAPI がまだ無ければ何も出せないので、その場合は無限ループにせず
/// `sys_exit` も呼べない — が、`bind()` は必ず `os32_init` の後に呼ばれる。
fn refuse(msg: &[u8], got: i32, want: i32) -> ! {
    unsafe {
        let p = crate::api_ptr();
        if !p.is_null() {
            let a: &KernelAPI = &*p;
            (a.kprintf)(
                crate::ATTR_RED,
                b"[shlib] %.*s: got=%d want=%d\r\n\0".as_ptr(),
                msg.len() as i32,
                msg.as_ptr(),
                got,
                want,
            );
            (a.kprintf)(
                crate::ATTR_YELLOW,
                b"[shlib] libos32gui.shlib mismatch - rebuild both (make clean; make all)\r\n\0"
                    .as_ptr(),
            );
            (a.sys_exit)(-1);
        }
    }
    loop {}
}

/// `MEM_SHLIB_BASE` のヘッダを照合してジャンプ表を捕まえ、`shlib_init` を呼ぶ。
///
/// 2 回目以降は何もしない。`os32_init()` の後に呼ぶこと (KAPI をライブラリへ渡す)。
/* Retained only when the stub is used; packaging checks this symbol after LTO. */
#[no_mangle]
#[inline(never)]
pub extern "C" fn __os32_shlib_dependency() -> u32 {
    static PROTOCOL: u32 = crate::generations::OS32_SHLIB_PROTOCOL;
    unsafe { core::ptr::read_volatile(&PROTOCOL) }
}

pub fn bind() {
    unsafe {
        let slot = TABLE.0.get();
        if !(*slot).is_null() {
            return;
        }
        let h = core::ptr::read_volatile(MEM_SHLIB_BASE as *const ShlibHeader);
        if h._rsvd[0] != __os32_shlib_dependency() || h.magic != OS32_SHLIB_MAGIC {
            refuse(b"bad magic at MEM_SHLIB_BASE", h.magic as i32, OS32_SHLIB_MAGIC as i32);
        }
        if h.version != GUI_PROTO_VERSION as u32 {
            refuse(b"version mismatch", h.version as i32, GUI_PROTO_VERSION as i32);
        }
        if (h.nfunc as usize) < SHLIB_NFUNC {
            refuse(b"jump table too short", h.nfunc as i32, SHLIB_NFUNC as i32);
        }
        let tbl = (MEM_SHLIB_BASE as usize + OS32_SHLIB_ENTRY_OFF) as *const u32;
        *slot = tbl;
        /* ライブラリへ KAPI を渡す (ライブラリの .data にある os32api の実体)。 */
        let init: extern "C" fn(*mut KernelAPI) -> i32 =
            core::mem::transmute_copy(&(*tbl.add(E_SHLIB_INIT) as usize));
        /* 初期化の失敗 (旧カーネルの KAPI を断った等、票 TASK_KAPI_DATA_FIELDS)
         * を捨てない。以後の呼び出しはライブラリの門で失敗するだけなので、
         * 黙って進まず理由を出して終わる。 */
        let rc = init(crate::api_ptr());
        if rc < 0 {
            refuse(b"shlib init failed", rc, 0);
        }
    }
}

/// 番号 `idx` のエントリを関数ポインタとして取り出す。
///
/// # Safety
/// `F` は表の該当エントリと同じ `extern "C"` シグネチャでなければならない。
#[inline]
pub unsafe fn fp<F: Copy>(idx: usize) -> F {
    debug_assert!(core::mem::size_of::<F>() == 4);
    debug_assert!(idx < SHLIB_NFUNC);
    let slot = TABLE.0.get();
    if (*slot).is_null() {
        bind();
    }
    let a = *(*slot).add(idx) as usize;
    core::mem::transmute_copy::<usize, F>(&a)
}

/// 束縛済みか (試験用)。
#[inline]
pub fn is_bound() -> bool {
    unsafe { !(*TABLE.0.get()).is_null() }
}

/// Fullscreen consumers have no OP_INIT slot / OP_WAIT loop. Reuse the
/// existing shlib initializer after their input wait or explicit mode change.
/// This does not reset pools or add an entry / protocol generation (e7).
pub fn check_gfx() -> i32 {
    unsafe {
        let init = fp::<extern "C" fn(*mut KernelAPI) -> i32>(E_SHLIB_INIT);
        let rc = init(crate::api_ptr());
        if rc < 0 { return rc; }
        let mut info = super::types::ScreenInfo::ZERO;
        fp::<extern "C" fn(*mut super::types::ScreenInfo)>(E_SCREEN_INFO)(&mut info);
        if info.width == 0 || info.height == 0 { super::proto::OS32_ERR_INVAL } else { 0 }
    }
}
