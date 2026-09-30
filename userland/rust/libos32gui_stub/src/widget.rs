//! widget.rs — 保持型ウィジェット木 (契約 U6) のスタブ。
//!
//! **木の実体 (64 個の固定配列・リスト項目 128) はライブラリ側の `.bss`** にあり、
//! アプリごとの物理ページになる (K3)。ここに残るのは `WidgetId` と値の定数だけ。

use crate::client::{ok0, GuiErr, GuiResult};
use crate::layout::SizeSpec;
use crate::shcall;
use os32api::gui::stub::{self as sh, SIZE_ABSOLUTE, SIZE_FIXED, SIZE_FLEX};
use os32api::gui::types::Rect;

pub use os32api::gui::stub::WidgetId;

/* PC-98 スキャンコード (drivers/kbd.h)。契約 U2a: `Key` は scan を運ぶ。 */
pub const SCAN_ESC: u8 = 0x00;
pub const SCAN_BS: u8 = 0x0E;
pub const SCAN_TAB: u8 = 0x0F;
pub const SCAN_RETURN: u8 = 0x1C;
pub const SCAN_SPACE: u8 = 0x34;
pub const SCAN_ROLLUP: u8 = 0x36;
pub const SCAN_ROLLDOWN: u8 = 0x37;
pub const SCAN_DEL: u8 = 0x39;
pub const SCAN_UP: u8 = 0x3A;
pub const SCAN_LEFT: u8 = 0x3B;
pub const SCAN_RIGHT: u8 = 0x3C;
pub const SCAN_DOWN: u8 = 0x3D;
pub const SCAN_HOME: u8 = 0x3E;
/// `kbd_get_modifiers` の SHIFT ビット (契約 U2a)。
pub const MOD_SHIFT: u8 = 0x01;

/// リスト 1 行の高さ (px)。
pub const LIST_ROW_H: i16 = 18;

/* 合成イベントの種別 (契約 U6 の `Widget{kind}`)。 */
pub const WEV_CLICK: u8 = 1;
pub const WEV_TEXT_CHANGED: u8 = 2;
pub const WEV_TOGGLED: u8 = 3;
pub const WEV_SELECT: u8 = 4;
pub const WEV_FOCUS: u8 = 5;

/// クライアント側で合成したウィジェットイベント (`App` のハンドラで受ける形)。
#[derive(Clone, Copy)]
pub struct WidgetEvent {
    pub kind: u8,
    pub widget: WidgetId,
    /// TOGGLED なら 0/1、SELECT なら選択 index、それ以外は 0。
    pub value: i32,
}

impl WidgetEvent {
    pub const NONE: WidgetEvent = WidgetEvent { kind: 0, widget: WidgetId::NULL, value: 0 };
}

/* ================================================================ */
/*  生成 (契約 U6)                                                    */
/* ================================================================ */

#[inline]
fn mk(r: i32, id: u32) -> GuiResult<WidgetId> {
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(WidgetId(id))
    }
}

/// 横並びコンテナ (契約 U7)。
pub fn row(pad: i16, gap: i16) -> GuiResult<WidgetId> {
    let mut id = 0u32;
    let r = shcall!(
        sh::E_W_ROW,
        extern "C" fn(i32, i32, *mut u32) -> i32,
        pad as i32,
        gap as i32,
        &mut id as *mut u32
    );
    mk(r, id)
}

/// 縦並びコンテナ (契約 U7)。
pub fn column(pad: i16, gap: i16) -> GuiResult<WidgetId> {
    let mut id = 0u32;
    let r = shcall!(
        sh::E_W_COLUMN,
        extern "C" fn(i32, i32, *mut u32) -> i32,
        pad as i32,
        gap as i32,
        &mut id as *mut u32
    );
    mk(r, id)
}

#[inline]
fn mk_text(idx: usize, text: &[u8]) -> GuiResult<WidgetId> {
    let mut id = 0u32;
    let r = shcall!(
        idx,
        extern "C" fn(*const u8, u32, *mut u32) -> i32,
        text.as_ptr(),
        text.len() as u32,
        &mut id as *mut u32
    );
    mk(r, id)
}

pub fn label(text: &[u8]) -> GuiResult<WidgetId> {
    mk_text(sh::E_W_LABEL, text)
}

pub fn button(text: &[u8]) -> GuiResult<WidgetId> {
    mk_text(sh::E_W_BUTTON, text)
}

pub fn checkbox(text: &[u8], checked: bool) -> GuiResult<WidgetId> {
    let mut id = 0u32;
    let r = shcall!(
        sh::E_W_CHECKBOX,
        extern "C" fn(*const u8, u32, u32, *mut u32) -> i32,
        text.as_ptr(),
        text.len() as u32,
        checked as u32,
        &mut id as *mut u32
    );
    mk(r, id)
}

pub fn textbox(text: &[u8]) -> GuiResult<WidgetId> {
    mk_text(sh::E_W_TEXTBOX, text)
}

pub fn listbox() -> GuiResult<WidgetId> {
    let mut id = 0u32;
    let r = shcall!(
        sh::E_W_LISTBOX,
        extern "C" fn(*mut u32) -> i32,
        &mut id as *mut u32
    );
    mk(r, id)
}

/// コンテナに子を足す (契約 U7)。
pub fn add(parent: WidgetId, child: WidgetId, size: SizeSpec) -> GuiResult<()> {
    let (kind, v, rect) = match size {
        SizeSpec::Fixed(px) => (SIZE_FIXED, px as i32, Rect::EMPTY),
        SizeSpec::Flex(w) => (SIZE_FLEX, w as i32, Rect::EMPTY),
        SizeSpec::Absolute(r) => (SIZE_ABSOLUTE, 0, r),
    };
    ok0(shcall!(
        sh::E_W_ADD,
        extern "C" fn(u32, u32, u32, i32, Rect) -> i32,
        parent.raw(),
        child.raw(),
        kind,
        v,
        rect
    ))
}

/// 交差軸の大きさを固定する (0 = 親いっぱい)。
pub fn set_cross(id: WidgetId, v: i16) {
    shcall!(
        sh::E_W_SET_CROSS,
        extern "C" fn(u32, i32),
        id.raw(),
        v as i32
    )
}

/// 最小サイズ。
pub fn set_min(id: WidgetId, w: i16, h: i16) {
    shcall!(
        sh::E_W_SET_MIN,
        extern "C" fn(u32, i32, i32),
        id.raw(),
        w as i32,
        h as i32
    )
}

/* ================================================================ */
/*  プロパティ (変更 → 自分の矩形を invalidate、契約 G4)              */
/* ================================================================ */

pub fn set_text(id: WidgetId, text: &[u8]) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_W_SET_TEXT,
        extern "C" fn(u32, *const u8, u32) -> i32,
        id.raw(),
        text.as_ptr(),
        text.len() as u32
    ))
}

/// 現在のテキストを `out` へ写す。戻り値: 書いたバイト数。
pub fn text(id: WidgetId, out: &mut [u8]) -> usize {
    shcall!(
        sh::E_W_GET_TEXT,
        extern "C" fn(u32, *mut u8, u32) -> u32,
        id.raw(),
        out.as_mut_ptr(),
        out.len() as u32
    ) as usize
}

pub fn set_checked(id: WidgetId, on: bool) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_W_SET_CHECKED,
        extern "C" fn(u32, u32) -> i32,
        id.raw(),
        on as u32
    ))
}

pub fn is_checked(id: WidgetId) -> bool {
    shcall!(sh::E_W_IS_CHECKED, extern "C" fn(u32) -> u32, id.raw()) != 0
}

pub fn set_enabled(id: WidgetId, on: bool) {
    shcall!(
        sh::E_W_SET_ENABLED,
        extern "C" fn(u32, u32),
        id.raw(),
        on as u32
    )
}

pub fn set_hidden(id: WidgetId, hidden: bool) {
    shcall!(
        sh::E_W_SET_HIDDEN,
        extern "C" fn(u32, u32),
        id.raw(),
        hidden as u32
    )
}

/// レイアウト後の矩形 (クライアントローカル)。
pub fn rect(id: WidgetId) -> Rect {
    let mut r = Rect::EMPTY;
    shcall!(
        sh::E_W_RECT,
        extern "C" fn(u32, *mut Rect),
        id.raw(),
        &mut r as *mut Rect
    );
    r
}

/* ================================================================ */
/*  リストボックス                                                    */
/* ================================================================ */

pub fn list_clear(id: WidgetId) -> GuiResult<()> {
    ok0(shcall!(sh::E_W_LIST_CLEAR, extern "C" fn(u32) -> i32, id.raw()))
}

/// 末尾に 1 行足す。戻り値: その index。
pub fn list_add(id: WidgetId, text: &[u8]) -> GuiResult<i32> {
    let mut index = 0i32;
    let r = shcall!(
        sh::E_W_LIST_ADD,
        extern "C" fn(u32, *const u8, u32, *mut i32) -> i32,
        id.raw(),
        text.as_ptr(),
        text.len() as u32,
        &mut index as *mut i32
    );
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(index)
    }
}

/// 選択中の index (未選択は -1)。
pub fn list_selection(id: WidgetId) -> i32 {
    shcall!(sh::E_W_LIST_SELECTION, extern "C" fn(u32) -> i32, id.raw())
}

pub fn list_set_selection(id: WidgetId, index: i32) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_W_LIST_SET_SELECTION,
        extern "C" fn(u32, i32) -> i32,
        id.raw(),
        index
    ))
}

/// `index` 行のテキストを `out` へ写す。戻り値: 書いたバイト数。
pub fn list_item_text(id: WidgetId, index: i32, out: &mut [u8]) -> usize {
    shcall!(
        sh::E_W_LIST_ITEM_TEXT,
        extern "C" fn(u32, i32, *mut u8, u32) -> u32,
        id.raw(),
        index,
        out.as_mut_ptr(),
        out.len() as u32
    ) as usize
}

/* ================================================================ */
/*  複数行の編集面 WK_TEXTAREA (票 TASK_EDIT_GUI §2、決裁 A1)         */
/*                                                                  */
/*  **本文は部品に無い。** アプリが本文を持ち、いま見えている行だけを  */
/*  `textarea_clear` + `textarea_add_row` で写す。1 画面に入らない     */
/*  ファイルでも部品の側の使用量は一定 (票 §2)。                       */
/*                                                                  */
/*  `textbox` (1 行) はそのまま。**触っていない** (受入 E10)。         */
/* ================================================================ */

/// 複数行の編集面を作る。
pub fn textarea() -> GuiResult<WidgetId> {
    let mut id = 0u32;
    let r = shcall!(
        sh::E_W_TEXTAREA,
        extern "C" fn(*mut u32) -> i32,
        &mut id as *mut u32
    );
    mk(r, id)
}

/// 写してある「見える行」を全部捨てる。
pub fn textarea_clear(id: WidgetId) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_W_TEXTAREA_CLEAR,
        extern "C" fn(u32) -> i32,
        id.raw()
    ))
}

/// 見える行を 1 本足す (上から順に)。戻りはその相対行番号。
/// 入り切らなければ `Err(FULL)` — **黙って切り詰めない**。
pub fn textarea_add_row(id: WidgetId, text: &[u8]) -> GuiResult<i32> {
    let mut index = 0i32;
    let r = shcall!(
        sh::E_W_TEXTAREA_ADD_ROW,
        extern "C" fn(u32, *const u8, u32, *mut i32) -> i32,
        id.raw(),
        text.as_ptr(),
        text.len() as u32,
        &mut index as *mut i32
    );
    if r < 0 {
        Err(GuiErr(r))
    } else {
        Ok(index)
    }
}

/// キャレットを置く (`row` = 見える範囲の先頭からの相対行、`col` = その行の
/// 先頭からの**バイト数**)。`row < 0` で消す。
pub fn textarea_set_caret(id: WidgetId, row: i32, col: i32) -> GuiResult<()> {
    ok0(shcall!(
        sh::E_W_TEXTAREA_SET_CARET,
        extern "C" fn(u32, i32, i32) -> i32,
        id.raw(),
        row,
        col
    ))
}

/// 矩形に入る行数。アプリはこれを見て見える範囲を切り出す ([C4])。
pub fn textarea_visible_rows(id: WidgetId) -> i32 {
    shcall!(
        sh::E_W_TEXTAREA_VISIBLE_ROWS,
        extern "C" fn(u32) -> i32,
        id.raw()
    )
}

/// 矩形に入る半角の**桁数** (全角は 2 桁)。折り返しの幅に使う ([C4])。
pub fn textarea_columns(id: WidgetId) -> i32 {
    shcall!(
        sh::E_W_TEXTAREA_COLUMNS,
        extern "C" fn(u32) -> i32,
        id.raw()
    )
}

/// 溜まった確定文字列を引き取る (引き取ったら空になる)。戻りは写したバイト数。
/// `out` が短ければ何も渡さず 0 (UTF-8 を割らない)。
pub fn textarea_take_input(id: WidgetId, out: &mut [u8]) -> usize {
    shcall!(
        sh::E_W_TEXTAREA_TAKE_INPUT,
        extern "C" fn(u32, *mut u8, u32) -> u32,
        id.raw(),
        out.as_mut_ptr(),
        out.len() as u32
    ) as usize
}

/* ================================================================ */
/*  フォーカスとハンドル                                              */
/* ================================================================ */

/// フォーカスを移す (契約 U6)。
pub fn set_focus(id: WidgetId) -> GuiResult<()> {
    ok0(shcall!(sh::E_W_SET_FOCUS, extern "C" fn(u32) -> i32, id.raw()))
}

/// 窓スロット `win` でフォーカス中のウィジェット。
pub fn focused(win: usize) -> WidgetId {
    WidgetId(shcall!(
        sh::E_W_FOCUSED,
        extern "C" fn(u32) -> u32,
        win as u32
    ))
}

/// 窓 (**WindowId**) でフォーカス中のウィジェット。無効な窓なら `NULL`。
///
/// [`focused`] は窓スロットの添字を取り、**アプリは添字を持っていない**。
/// アプリはこちらを使う (穴 H13 の後始末、ジャンプ表 118)。
pub fn focused_in(window: u32) -> WidgetId {
    WidgetId(shcall!(
        sh::E_W_FOCUSED_IN,
        extern "C" fn(u32) -> u32,
        window
    ))
}

/// `WidgetId` からスロット添字を引く (generation 検査つき)。
pub fn resolve(id: WidgetId) -> Option<usize> {
    let r = shcall!(sh::E_W_RESOLVE, extern "C" fn(u32) -> i32, id.raw());
    if r < 0 {
        None
    } else {
        Some(r as usize)
    }
}

/// スロット添字から現在の `WidgetId` を作る。
pub fn id_of(idx: usize) -> WidgetId {
    WidgetId(shcall!(sh::E_W_ID_OF, extern "C" fn(u32) -> u32, idx as u32))
}
