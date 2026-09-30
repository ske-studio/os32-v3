//! uistate.rs — クライアント側 (アプリの空間) の保持型 UI 状態 (契約 U6)。
//!
//! ウィンドウ登録表 (16) / ウィジェット木 (64) / リスト項目プール (128) /
//! 1 周分の `Paint` 待ち行列を、**固定長配列 1 つのグローバル**に置く。
//! alloc は使わない (v2 CONTRACTS C8)。単一スレッド (CPL=3 の 1 アプリ) 前提。
//!
//! WM はウィンドウしか知らない。ウィジェットの描画とイベント合成はすべて
//! この空間で完結し、syscall はゼロ (契約 U6)。
#![allow(dead_code)]

use core::cell::UnsafeCell;

use crate::layout::SizeSpec;
use os32api::gui::proto::{
    GUI_MAX_LIST_ITEMS, GUI_MAX_TEXTAREA_ROWS, GUI_MAX_WIDGETS, GUI_MAX_WINDOWS,
    GUI_TEXTAREA_INPUT_CAP, GUI_TEXTAREA_ROW_CAP,
};
use os32api::gui::types::{Rect, SurfaceId};

/// ウィジェット 1 個が持てるテキスト長 (UTF-8、NUL を含まない)。
pub const TEXT_CAP: usize = 64;
/// リスト項目 1 件のテキスト長。
pub const ITEM_TEXT_CAP: usize = 40;
/// 1 周で保持できる `Paint` 矩形 (WM の可視領域上限と同じ 16)。
pub const MAX_PAINT: usize = 16;
/// 木のリンク (index + 1) の「無し」。
pub const GUI_NONE: u16 = 0;

/* ================================================================ */
/*  ウィジェットの種別 (契約 U6) — v1 は 5 種 + コンテナ 2 種         */
/* ================================================================ */
pub const WK_NONE: u8 = 0;
pub const WK_LABEL: u8 = 1;
pub const WK_BUTTON: u8 = 2;
pub const WK_CHECKBOX: u8 = 3;
pub const WK_TEXTBOX: u8 = 4;
pub const WK_LISTBOX: u8 = 5;
pub const WK_ROW: u8 = 6;
pub const WK_COLUMN: u8 = 7;
/// 複数行の編集面 (票 TASK_EDIT_GUI §2、決裁 A1: **末尾追記**)。
///
/// `WK_TEXTBOX` (4) は 1 行のまま**触らない**。既存の利用者 (filer ほか) の
/// 見え方と振る舞いを変えないため (受入 E10)。
///
/// **本文は持たない**。アプリが本文を持ち、いま見えている行だけを
/// [`TextRow`] プールへ写す。1 画面に入らないファイルでも部品の側は一定。
pub const WK_TEXTAREA: u8 = 8;

#[inline]
pub fn is_container(kind: u8) -> bool {
    kind == WK_ROW || kind == WK_COLUMN
}

#[inline]
pub fn is_focusable(kind: u8) -> bool {
    kind == WK_BUTTON
        || kind == WK_CHECKBOX
        || kind == WK_TEXTBOX
        || kind == WK_LISTBOX
        || kind == WK_TEXTAREA
}

/* ウィジェットの状態ビット */
pub const WFL_CHECKED: u8 = 0x01;
pub const WFL_PRESSED: u8 = 0x02;
pub const WFL_DISABLED: u8 = 0x04;
pub const WFL_HIDDEN: u8 = 0x08;

/* ================================================================ */
/*  ウィジェット表                                                   */
/* ================================================================ */

#[derive(Clone, Copy)]
pub struct WidgetEnt {
    pub used: bool,
    pub generation: u16,
    /// 所属ウィンドウの WindowId (0 = 無所属)。
    pub window: u32,
    pub kind: u8,
    /* 木構造 (index + 1。0 = 無し) */
    pub parent: u16,
    pub first_child: u16,
    pub next_sibling: u16,
    /// 箱レイアウトが決めた矩形 (クライアントローカル)。
    pub rect: Rect,
    pub size: SizeSpec,
    pub min_w: i16,
    pub min_h: i16,
    /// 交差軸の大きさ (0 = 親いっぱいに広げる)。
    pub cross: i16,
    /// コンテナの内側余白と子の間隔。
    pub pad: i16,
    pub gap: i16,
    pub text: [u8; TEXT_CAP],
    pub text_len: u8,
    pub flags: u8,
    /* textbox */
    pub caret: i16,
    /* listbox */
    pub sel: i16,
    pub top: i16,
    pub item_count: i16,
    /* textarea (WK_TEXTAREA)。**本文は持たない** — 見えている行の本数と、
     * その中でのキャレット位置だけ。行の実体は `UiState::rows`。 */
    /// 写してある見える行の本数。
    pub ta_rows: i16,
    /// キャレットの行 (見える範囲の先頭からの相対。-1 = 出さない)。
    pub ta_caret_row: i16,
    /// キャレットの桁 (その行の先頭からのバイト数)。
    pub ta_caret_col: i16,
    /// 溜めておいた確定文字列 (アプリが `textarea_take_input` で引き取る)。
    pub ta_input: [u8; GUI_TEXTAREA_INPUT_CAP],
    pub ta_input_len: u8,
}

impl WidgetEnt {
    pub const EMPTY: WidgetEnt = WidgetEnt {
        used: false,
        generation: 0,
        window: 0,
        kind: WK_NONE,
        parent: 0,
        first_child: 0,
        next_sibling: 0,
        rect: Rect::EMPTY,
        size: SizeSpec::Flex(1),
        min_w: 0,
        min_h: 0,
        cross: 0,
        pad: 0,
        gap: 0,
        text: [0; TEXT_CAP],
        text_len: 0,
        flags: 0,
        caret: 0,
        sel: -1,
        top: 0,
        item_count: 0,
        ta_rows: 0,
        ta_caret_row: -1,
        ta_caret_col: 0,
        ta_input: [0; GUI_TEXTAREA_INPUT_CAP],
        ta_input_len: 0,
    };

    #[inline]
    pub fn text_slice(&self) -> &[u8] {
        &self.text[..self.text_len as usize]
    }

    #[inline]
    pub fn has(&self, f: u8) -> bool {
        (self.flags & f) != 0
    }
}

/* ================================================================ */
/*  リスト項目プール                                                 */
/* ================================================================ */

#[derive(Clone, Copy)]
pub struct ListItem {
    pub used: bool,
    /// 所属ウィジェットの index + 1 (0 = 未使用)。
    pub owner: u16,
    pub index: i16,
    pub text: [u8; ITEM_TEXT_CAP],
    pub len: u8,
}

impl ListItem {
    pub const EMPTY: ListItem = ListItem {
        used: false,
        owner: 0,
        index: 0,
        text: [0; ITEM_TEXT_CAP],
        len: 0,
    };

    #[inline]
    pub fn text_slice(&self) -> &[u8] {
        &self.text[..self.len as usize]
    }
}

/* ================================================================ */
/*  textarea の「見える行」プール (票 TASK_EDIT_GUI §2)               */
/*                                                                  */
/*  listbox の項目プールと同じ作り (owner タグ + index)。違うのは     */
/*  **1 行が 192B** (80 桁 = 全角 40 字 * 3B) であることと、本数が     */
/*  「1 画面に入る行数」で決まること。ファイルの大きさとは無関係で、  */
/*  ここが一定だから大きなファイルでも破綻しない。                    */
/* ================================================================ */

#[derive(Clone, Copy)]
pub struct TextRow {
    pub used: bool,
    /// 所属ウィジェットの index + 1 (0 = 未使用)。
    pub owner: u16,
    /// 見える範囲の先頭からの相対行番号。
    pub index: i16,
    pub text: [u8; GUI_TEXTAREA_ROW_CAP],
    pub len: u16,
}

impl TextRow {
    pub const EMPTY: TextRow = TextRow {
        used: false,
        owner: 0,
        index: 0,
        text: [0; GUI_TEXTAREA_ROW_CAP],
        len: 0,
    };

    #[inline]
    pub fn text_slice(&self) -> &[u8] {
        &self.text[..self.len as usize]
    }
}

/* ================================================================ */
/*  ウィンドウ登録表 (所有型 `Window` の実体側)                       */
/* ================================================================ */

#[derive(Clone, Copy)]
pub struct WinEnt {
    pub used: bool,
    /// WM が返した WindowId (index:16 | generation:16)。0 = 未使用。
    pub id: u32,
    /// クライアント面のサーフェス (C1 の `create_window_surface`)。
    pub surface: SurfaceId,
    /// クライアント矩形。原点は**画面絶対**、大きさはクライアント面 (W1 の規約)。
    pub client: Rect,
    /// 根コンテナ (index + 1。0 = 無し)。
    pub root: u16,
    /// キーボードフォーカス中のウィジェット (index + 1。0 = 無し)。
    pub focus: u16,
    /// 押下でアームしたウィジェット (index + 1)。
    pub armed: u16,
    /// WM から `Focus{in}` を受けているか。
    pub focused: bool,
}

impl WinEnt {
    pub const EMPTY: WinEnt = WinEnt {
        used: false,
        id: 0,
        surface: SurfaceId::NULL,
        client: Rect::EMPTY,
        root: 0,
        focus: 0,
        armed: 0,
        focused: false,
    };
}

/* ================================================================ */
/*  1 周分の Paint 待ち行列 (契約 U3: handle では描かない)            */
/* ================================================================ */

#[derive(Clone, Copy)]
pub struct PaintReq {
    pub window: u32,
    pub rect: Rect,
}

pub struct PaintQueue {
    pub items: [PaintReq; MAX_PAINT],
    pub len: usize,
}

impl PaintQueue {
    pub const EMPTY: PaintQueue = PaintQueue {
        items: [PaintReq { window: 0, rect: Rect::EMPTY }; MAX_PAINT],
        len: 0,
    };

    /// 追加する。満杯なら同じ窓の先頭項目と union して数を保つ (落とさない)。
    pub fn push(&mut self, window: u32, rect: Rect) {
        if rect.is_empty() {
            return;
        }
        if self.len < MAX_PAINT {
            self.items[self.len] = PaintReq { window, rect };
            self.len += 1;
            return;
        }
        let mut i = 0;
        while i < self.len {
            if self.items[i].window == window {
                self.items[i].rect = union(self.items[i].rect, rect);
                return;
            }
            i += 1;
        }
        self.items[0].rect = union(self.items[0].rect, rect);
        self.items[0].window = window;
    }

    #[inline]
    pub fn clear(&mut self) {
        self.len = 0;
    }
}

/* ================================================================ */
/*  損傷の溜め置き (契約 P: 1 周の gui_call を減らす)                 */
/*                                                                  */
/*  プロパティ変更のたびに `OP_INVALIDATE` を撃つと 1 周の gui_call が */
/*  すぐ 10 本 (契約 P の上限) を超える。1 周分をここに溜め、重なる /   */
/*  隣り合う矩形を束ねてから **窓あたり最大 8 本** (契約 P の損傷矩形の */
/*  上限と同じ) で送る。                                              */
/* ================================================================ */
pub const MAX_DAMAGE: usize = 8;

pub struct DamageBuf {
    pub items: [PaintReq; MAX_DAMAGE],
    pub len: usize,
}

impl DamageBuf {
    pub const EMPTY: DamageBuf = DamageBuf {
        items: [PaintReq { window: 0, rect: Rect::EMPTY }; MAX_DAMAGE],
        len: 0,
    };

    pub fn push(&mut self, window: u32, rect: Rect) {
        if rect.is_empty() || window == 0 {
            return;
        }
        /* 同じ窓で重なる / 隣り合うものは束ねる。 */
        let mut i = 0;
        while i < self.len {
            if self.items[i].window == window && near(self.items[i].rect, rect) {
                self.items[i].rect = union(self.items[i].rect, rect);
                return;
            }
            i += 1;
        }
        if self.len < MAX_DAMAGE {
            self.items[self.len] = PaintReq { window, rect };
            self.len += 1;
            return;
        }
        /* 溢れたら同じ窓の先頭へ union (落とさない)。 */
        let mut k = 0;
        while k < self.len {
            if self.items[k].window == window {
                self.items[k].rect = union(self.items[k].rect, rect);
                return;
            }
            k += 1;
        }
        self.items[0].rect = union(self.items[0].rect, rect);
        self.items[0].window = window;
    }

    #[inline]
    pub fn clear(&mut self) {
        self.len = 0;
    }
}

/// 重なる、または 8px 以内に隣り合うか (束ねてよいか)。
fn near(a: Rect, b: Rect) -> bool {
    let grown = Rect::new(a.x - 8, a.y - 8, a.w + 16, a.h + 16);
    !grown.intersect(&b).is_empty()
}

/// 2 矩形を包む最小矩形 (空は相手をそのまま返す)。
pub fn union(a: Rect, b: Rect) -> Rect {
    if a.is_empty() {
        return b;
    }
    if b.is_empty() {
        return a;
    }
    let x0 = if a.x < b.x { a.x } else { b.x };
    let y0 = if a.y < b.y { a.y } else { b.y };
    let x1 = if a.right() > b.right() { a.right() } else { b.right() };
    let y1 = if a.bottom() > b.bottom() { a.bottom() } else { b.bottom() };
    Rect::new(x0, y0, (x1 - x0 as i32) as i16, (y1 - y0 as i32) as i16)
}

/* ================================================================ */
/*  グローバル状態                                                   */
/* ================================================================ */

pub struct UiState {
    pub windows: [WinEnt; GUI_MAX_WINDOWS],
    pub widgets: [WidgetEnt; GUI_MAX_WIDGETS],
    pub items: [ListItem; GUI_MAX_LIST_ITEMS],
    /// textarea の見える行 (全 textarea で共有。契約の上限は共有定数)。
    pub rows: [TextRow; GUI_MAX_TEXTAREA_ROWS],
    pub paint: PaintQueue,
    /// 1 周分の損傷 (`flush_damage` で `OP_INVALIDATE` にまとめて送る)。
    pub damage: DamageBuf,
    /// `Quit` / 全ウィンドウ破棄 / アプリの要求で立つ。
    pub quit: bool,
    /// `OVERFLOW` を受けた直後は入力状態を未知として扱う (契約 T3)。
    pub input_unknown: bool,
    /// マウスの最新位置 (クライアントローカル、フォーカス窓基準)。
    pub ptr_x: i16,
    pub ptr_y: i16,
    /// アプリが `widget::set_focus` で移したフォーカスの通知待ち
    /// (生の `WidgetId`、0 = 無し)。入力から合成したぶんは `WidgetOut` で
    /// 返るが、アプリ発は返す先が無いのでここに溜めてループが配る (穴 H13)。
    pub pending_focus: u32,
}

impl UiState {
    pub const NEW: UiState = UiState {
        windows: [WinEnt::EMPTY; GUI_MAX_WINDOWS],
        widgets: [WidgetEnt::EMPTY; GUI_MAX_WIDGETS],
        items: [ListItem::EMPTY; GUI_MAX_LIST_ITEMS],
        rows: [TextRow::EMPTY; GUI_MAX_TEXTAREA_ROWS],
        paint: PaintQueue::EMPTY,
        damage: DamageBuf::EMPTY,
        quit: false,
        input_unknown: false,
        ptr_x: 0,
        ptr_y: 0,
        pending_focus: 0,
    };

    /// WindowId から登録表の添字を引く (generation ごと一致。契約 U2)。
    pub fn win_slot(&self, id: u32) -> Option<usize> {
        if id == 0 {
            return None;
        }
        let mut i = 0;
        while i < GUI_MAX_WINDOWS {
            if self.windows[i].used && self.windows[i].id == id {
                return Some(i);
            }
            i += 1;
        }
        None
    }

    pub fn alloc_win(&mut self) -> Option<usize> {
        let mut i = 0;
        while i < GUI_MAX_WINDOWS {
            if !self.windows[i].used {
                self.windows[i] = WinEnt::EMPTY;
                self.windows[i].used = true;
                return Some(i);
            }
            i += 1;
        }
        None
    }

    /// ウィジェットスロットを 1 つ確保する (generation を進める)。
    pub fn alloc_widget(&mut self) -> Option<usize> {
        let mut i = 0;
        while i < GUI_MAX_WIDGETS {
            if !self.widgets[i].used {
                let gen = {
                    let g = self.widgets[i].generation.wrapping_add(1);
                    if g == 0 {
                        1
                    } else {
                        g
                    }
                };
                self.widgets[i] = WidgetEnt::EMPTY;
                self.widgets[i].used = true;
                self.widgets[i].generation = gen;
                return Some(i);
            }
            i += 1;
        }
        None
    }

    /// ウィジェット 1 個を解放する (リスト項目・見える行も)。木からの切り離しは呼び出し側。
    pub fn free_widget(&mut self, idx: usize) {
        let gen = self.widgets[idx].generation;
        self.widgets[idx] = WidgetEnt::EMPTY;
        self.widgets[idx].generation = gen;
        let mut k = 0;
        while k < GUI_MAX_LIST_ITEMS {
            if self.items[k].used && self.items[k].owner == (idx as u16) + 1 {
                self.items[k] = ListItem::EMPTY;
            }
            k += 1;
        }
        let mut r = 0;
        while r < GUI_MAX_TEXTAREA_ROWS {
            if self.rows[r].used && self.rows[r].owner == (idx as u16) + 1 {
                self.rows[r] = TextRow::EMPTY;
            }
            r += 1;
        }
    }
}

struct Cell(UnsafeCell<UiState>);
unsafe impl Sync for Cell {}
static STATE: Cell = Cell(UnsafeCell::new(UiState::NEW));

/// グローバル UI 状態 (単一スレッド前提)。
#[inline]
pub fn s() -> &'static mut UiState {
    unsafe { &mut *STATE.0.get() }
}
