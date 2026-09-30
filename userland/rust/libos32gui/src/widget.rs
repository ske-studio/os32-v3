//! widget.rs — 保持型ウィジェット木 (契約 U6)。票 C2 作業 5。
//!
//! - ウィジェットは**アプリの空間**にあり、WM はウィンドウしか知らない。描画は
//!   in-process (C1 の G API)、syscall はゼロ。
//! - `WidgetId` は generation 付き (`index:16 | generation:16`)。破棄後の ID は
//!   `ERR_STALE` を返す (契約 T4)。
//! - **プロパティ変更 → 自分の矩形を `invalidate`**。全面再描画はしない (契約 G4)。
//! - `Pointer` / `Button` / `Key` / `Text` から `Widget{kind}` を合成して返す。
//!   種別ごとのハンドラ (`App::on_click` 等) は `app.rs` が呼ぶ。
//!
//! 固定配列: ウィジェット 64 / リスト項目 128 (契約 U6)。

use crate::client::{self, utf8_seq_len, GuiErr, GuiResult};
use crate::layout::SizeSpec;
use crate::textcore;
use crate::uistate::{
    is_container, is_focusable, s, ListItem, TextRow, GUI_NONE, ITEM_TEXT_CAP, TEXT_CAP,
    WFL_CHECKED, WFL_DISABLED, WFL_HIDDEN, WFL_PRESSED, WK_BUTTON, WK_CHECKBOX, WK_COLUMN,
    WK_LABEL, WK_LISTBOX, WK_ROW, WK_TEXTAREA, WK_TEXTBOX,
};
use os32api::gui::proto::{
    GUI_COLOR_DISABLED, GUI_COLOR_EDIT_BG, GUI_COLOR_FACE, GUI_COLOR_HIGHLIGHT, GUI_COLOR_LIGHT,
    GUI_COLOR_SEL_BG, GUI_COLOR_SEL_TEXT, GUI_COLOR_SHADOW, GUI_COLOR_TEXT, GUI_COLOR_WINDOW,
    GUI_MAX_LIST_ITEMS, GUI_MAX_TEXTAREA_ROWS, GUI_MAX_WIDGETS, GUI_STYLE_DOTTED,
    GUI_TEXTAREA_INPUT_CAP, GUI_TEXTAREA_ROW_CAP,
};
use os32api::gui::types::{Rect, Style, SurfaceId};

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
/// チェックボックスの箱の一辺 (px)。
const CHECK_BOX: i16 = 14;
/// KCG セルの高さ (px、scale1)。
const CELL_H: i16 = 16;
/// KCG 半角 1 桁の幅 (px、scale1)。`draw.rs` の `ANK_W` と同じ。
const ANK_W: i16 = 8;
/// textarea の 1 行の高さ (px)。行間は入れない (エディタなので密に置く)。
pub const TEXTAREA_ROW_H: i16 = CELL_H;
/// 編集面の内側余白 (左右) — textbox と textarea で共通。
const EDIT_PAD_X: i16 = 3;

/* ================================================================ */
/*  合成イベント (契約 U6 の `Widget{kind}`)                          */
/*                                                                  */
/*  **PM への申し送り**: この kind 番号は共有ヘッダに無い (クライアント */
/*  内で閉じるため)。WM 経由で配送する必要が出たら os32_gui_shared.h へ */
/*  末尾追記が要る。                                                  */
/* ================================================================ */
pub const WEV_CLICK: u8 = 1;
pub const WEV_TEXT_CHANGED: u8 = 2;
pub const WEV_TOGGLED: u8 = 3;
pub const WEV_SELECT: u8 = 4;
pub const WEV_FOCUS: u8 = 5;

/// クライアント側で合成したウィジェットイベント。
#[derive(Clone, Copy)]
pub struct WidgetEvent {
    pub kind: u8,
    pub widget: WidgetId,
    /// TOGGLED なら 0/1、SELECT なら選択 index、それ以外は 0。
    pub value: i32,
}

impl WidgetEvent {
    pub const NONE: WidgetEvent =
        WidgetEvent { kind: 0, widget: WidgetId::NULL, value: 0 };
}

/// 1 つの入力から合成されるイベント (最大 2 件: フォーカス移動 + 本体)。
pub struct WidgetOut {
    pub evs: [WidgetEvent; 2],
    pub n: usize,
}

impl WidgetOut {
    pub const EMPTY: WidgetOut = WidgetOut { evs: [WidgetEvent::NONE; 2], n: 0 };

    fn push(&mut self, kind: u8, widget: WidgetId, value: i32) {
        if self.n < 2 {
            self.evs[self.n] = WidgetEvent { kind, widget, value };
            self.n += 1;
        }
    }

    #[inline]
    pub fn as_slice(&self) -> &[WidgetEvent] {
        &self.evs[..self.n]
    }
}

/* ================================================================ */
/*  WidgetId (index:16 | generation:16)                              */
/*                                                                  */
/*  正典は os32api::gui::stub — `App` のハンドラ引数に出るので、       */
/*  共有ライブラリ (票 C3) の両側が同じ型を見る必要がある。            */
/* ================================================================ */

pub use os32api::gui::stub::WidgetId;

/// `WidgetId` からスロット添字を引く (generation 検査つき)。
pub fn resolve(id: WidgetId) -> Option<usize> {
    if id.is_null() {
        return None;
    }
    let idx = id.index() as usize;
    if idx >= GUI_MAX_WIDGETS {
        return None;
    }
    let w = &s().widgets[idx];
    if w.used && w.generation == id.generation() {
        Some(idx)
    } else {
        None
    }
}

#[inline]
pub fn id_of(idx: usize) -> WidgetId {
    WidgetId::new(idx as u16, s().widgets[idx].generation)
}

/* ================================================================ */
/*  生成 (契約 U6)                                                    */
/* ================================================================ */

fn make(kind: u8, text: &[u8]) -> GuiResult<WidgetId> {
    let idx = match s().alloc_widget() {
        Some(i) => i,
        None => return Err(GuiErr::FULL),
    };
    s().widgets[idx].kind = kind;
    store_text(idx, text);
    if kind == WK_LISTBOX {
        s().widgets[idx].sel = -1;
    }
    Ok(id_of(idx))
}

/// 横並びコンテナ (契約 U7)。
pub fn row(pad: i16, gap: i16) -> GuiResult<WidgetId> {
    let id = make(WK_ROW, b"")?;
    let i = resolve(id).unwrap();
    s().widgets[i].pad = pad;
    s().widgets[i].gap = gap;
    Ok(id)
}

/// 縦並びコンテナ (契約 U7)。
pub fn column(pad: i16, gap: i16) -> GuiResult<WidgetId> {
    let id = make(WK_COLUMN, b"")?;
    let i = resolve(id).unwrap();
    s().widgets[i].pad = pad;
    s().widgets[i].gap = gap;
    Ok(id)
}

pub fn label(text: &[u8]) -> GuiResult<WidgetId> {
    let id = make(WK_LABEL, text)?;
    let i = resolve(id).unwrap();
    s().widgets[i].min_h = CELL_H;
    Ok(id)
}

pub fn button(text: &[u8]) -> GuiResult<WidgetId> {
    let id = make(WK_BUTTON, text)?;
    let i = resolve(id).unwrap();
    s().widgets[i].min_h = 22;
    Ok(id)
}

pub fn checkbox(text: &[u8], checked: bool) -> GuiResult<WidgetId> {
    let id = make(WK_CHECKBOX, text)?;
    let i = resolve(id).unwrap();
    s().widgets[i].min_h = CHECK_BOX + 2;
    if checked {
        s().widgets[i].flags |= WFL_CHECKED;
    }
    Ok(id)
}

pub fn textbox(text: &[u8]) -> GuiResult<WidgetId> {
    let id = make(WK_TEXTBOX, text)?;
    let i = resolve(id).unwrap();
    s().widgets[i].min_h = 20;
    s().widgets[i].caret = s().widgets[i].text_len as i16;
    Ok(id)
}

/// 複数行の編集面 (決裁 A1: `WK_TEXTBOX` は 1 行のまま。これは**新しい種別**)。
///
/// **本文は持たない。** アプリが本文を持ち、[`textarea_set_rows`] で
/// いま見えている行だけを写す。だから 1 画面に入らないファイルでも
/// 部品の側の使用量は一定で、破綻しない (票 §2)。
pub fn textarea() -> GuiResult<WidgetId> {
    let id = make(WK_TEXTAREA, b"")?;
    Ok(id)
}

pub fn listbox() -> GuiResult<WidgetId> {
    let id = make(WK_LISTBOX, b"")?;
    let i = resolve(id).unwrap();
    s().widgets[i].min_h = LIST_ROW_H + 2;
    Ok(id)
}

/// 子を末尾に足し、主軸の決め方を与える (契約 U7)。
pub fn add(parent: WidgetId, child: WidgetId, size: SizeSpec) -> GuiResult<()> {
    let p = resolve(parent).ok_or(GuiErr::STALE)?;
    let c = resolve(child).ok_or(GuiErr::STALE)?;
    if !is_container(s().widgets[p].kind) {
        return Err(GuiErr::INVAL);
    }
    s().widgets[c].size = size;
    s().widgets[c].parent = (p as u16) + 1;
    s().widgets[c].next_sibling = GUI_NONE;

    let first = s().widgets[p].first_child;
    if first == GUI_NONE {
        s().widgets[p].first_child = (c as u16) + 1;
    } else {
        let mut k = first as usize - 1;
        while s().widgets[k].next_sibling != GUI_NONE {
            k = s().widgets[k].next_sibling as usize - 1;
        }
        s().widgets[k].next_sibling = (c as u16) + 1;
    }
    let win = s().widgets[p].window;
    if win != 0 {
        assign_window(c, win);
    }
    Ok(())
}

/// 交差軸の大きさを固定する (0 = 親いっぱい)。
pub fn set_cross(id: WidgetId, v: i16) {
    if let Some(i) = resolve(id) {
        s().widgets[i].cross = v;
    }
}

/// 下限を与える (箱レイアウトが守る)。
pub fn set_min(id: WidgetId, w: i16, h: i16) {
    if let Some(i) = resolve(id) {
        s().widgets[i].min_w = w;
        s().widgets[i].min_h = h;
    }
}

/// 木にウィンドウ ID を配る (根に付けたときと `add` のとき)。
pub fn assign_window(idx: usize, window: u32) {
    s().widgets[idx].window = window;
    let mut c = s().widgets[idx].first_child;
    while c != GUI_NONE {
        let ci = c as usize - 1;
        assign_window(ci, window);
        c = s().widgets[ci].next_sibling;
    }
}

/// 部分木をまるごと解放する。
pub fn free_tree(idx: usize) {
    let mut c = s().widgets[idx].first_child;
    while c != GUI_NONE {
        let ci = c as usize - 1;
        let next = s().widgets[ci].next_sibling;
        free_tree(ci);
        c = next;
    }
    s().free_widget(idx);
}

/* ================================================================ */
/*  プロパティ (変更 → 自分の矩形を invalidate。契約 U6)              */
/* ================================================================ */

fn store_text(idx: usize, text: &[u8]) {
    let n = client::utf8_truncate(text, TEXT_CAP);
    let mut i = 0;
    while i < n {
        s().widgets[idx].text[i] = text[i];
        i += 1;
    }
    s().widgets[idx].text_len = n as u8;
}

/// 自分の矩形だけを損傷として申告する (1 周分は溜めてから送る。契約 P)。
pub fn invalidate(idx: usize) {
    let (win, rect) = {
        let w = &s().widgets[idx];
        (w.window, w.rect)
    };
    /* フォーカスの点線枠は矩形の内側 2px なので、そのままで足りる。 */
    s().damage.push(win, rect);
}

pub fn set_text(id: WidgetId, text: &[u8]) -> GuiResult<()> {
    let i = resolve(id).ok_or(GuiErr::STALE)?;
    store_text(i, text);
    if s().widgets[i].kind == WK_TEXTBOX {
        s().widgets[i].caret = s().widgets[i].text_len as i16;
    }
    invalidate(i);
    Ok(())
}

/// テキストを呼び出し側バッファへ写す。返り値: バイト数。
pub fn text(id: WidgetId, out: &mut [u8]) -> usize {
    let i = match resolve(id) {
        Some(i) => i,
        None => return 0,
    };
    let src = s().widgets[i].text_slice();
    let n = if src.len() < out.len() { src.len() } else { out.len() };
    out[..n].copy_from_slice(&src[..n]);
    n
}

pub fn set_checked(id: WidgetId, on: bool) -> GuiResult<()> {
    let i = resolve(id).ok_or(GuiErr::STALE)?;
    if on {
        s().widgets[i].flags |= WFL_CHECKED;
    } else {
        s().widgets[i].flags &= !WFL_CHECKED;
    }
    invalidate(i);
    Ok(())
}

pub fn is_checked(id: WidgetId) -> bool {
    match resolve(id) {
        Some(i) => s().widgets[i].has(WFL_CHECKED),
        None => false,
    }
}

pub fn set_enabled(id: WidgetId, on: bool) {
    if let Some(i) = resolve(id) {
        if on {
            s().widgets[i].flags &= !WFL_DISABLED;
        } else {
            s().widgets[i].flags |= WFL_DISABLED;
        }
        invalidate(i);
    }
}

pub fn set_hidden(id: WidgetId, hidden: bool) {
    if let Some(i) = resolve(id) {
        if hidden {
            s().widgets[i].flags |= WFL_HIDDEN;
        } else {
            s().widgets[i].flags &= !WFL_HIDDEN;
        }
        invalidate(i);
    }
}

/// ウィジェットの矩形 (クライアントローカル)。
pub fn rect(id: WidgetId) -> Rect {
    match resolve(id) {
        Some(i) => s().widgets[i].rect,
        None => Rect::EMPTY,
    }
}

/* ---- リストボックス ---- */

pub fn list_clear(id: WidgetId) -> GuiResult<()> {
    let i = resolve(id).ok_or(GuiErr::STALE)?;
    let mut k = 0;
    while k < GUI_MAX_LIST_ITEMS {
        if s().items[k].used && s().items[k].owner == (i as u16) + 1 {
            s().items[k] = ListItem::EMPTY;
        }
        k += 1;
    }
    s().widgets[i].item_count = 0;
    s().widgets[i].sel = -1;
    s().widgets[i].top = 0;
    invalidate(i);
    Ok(())
}

/// 項目を末尾に足す。返り値: その index。
pub fn list_add(id: WidgetId, text: &[u8]) -> GuiResult<i32> {
    let i = resolve(id).ok_or(GuiErr::STALE)?;
    if s().widgets[i].kind != WK_LISTBOX {
        return Err(GuiErr::INVAL);
    }
    let mut k = 0;
    while k < GUI_MAX_LIST_ITEMS {
        if !s().items[k].used {
            let index = s().widgets[i].item_count;
            let n = client::utf8_truncate(text, ITEM_TEXT_CAP);
            s().items[k] = ListItem::EMPTY;
            s().items[k].used = true;
            s().items[k].owner = (i as u16) + 1;
            s().items[k].index = index;
            let mut b = 0;
            while b < n {
                s().items[k].text[b] = text[b];
                b += 1;
            }
            s().items[k].len = n as u8;
            s().widgets[i].item_count = index + 1;
            if s().widgets[i].sel < 0 {
                s().widgets[i].sel = 0;
            }
            invalidate(i);
            return Ok(index as i32);
        }
        k += 1;
    }
    Err(GuiErr::FULL)
}

pub fn list_selection(id: WidgetId) -> i32 {
    match resolve(id) {
        Some(i) => s().widgets[i].sel as i32,
        None => -1,
    }
}

pub fn list_set_selection(id: WidgetId, index: i32) -> GuiResult<()> {
    let i = resolve(id).ok_or(GuiErr::STALE)?;
    if index < -1 || index >= s().widgets[i].item_count as i32 {
        return Err(GuiErr::INVAL);
    }
    s().widgets[i].sel = index as i16;
    ensure_visible(i);
    invalidate(i);
    Ok(())
}

/// 項目テキストを呼び出し側バッファへ。返り値: バイト数。
pub fn list_item_text(id: WidgetId, index: i32, out: &mut [u8]) -> usize {
    let i = match resolve(id) {
        Some(i) => i,
        None => return 0,
    };
    match item_slot(i, index as i16) {
        Some(k) => {
            let src = s().items[k].text_slice();
            let n = if src.len() < out.len() { src.len() } else { out.len() };
            out[..n].copy_from_slice(&src[..n]);
            n
        }
        None => 0,
    }
}

fn item_slot(widget_idx: usize, index: i16) -> Option<usize> {
    let st = s();
    let mut k = 0;
    while k < GUI_MAX_LIST_ITEMS {
        if st.items[k].used && st.items[k].owner == (widget_idx as u16) + 1 && st.items[k].index == index
        {
            return Some(k);
        }
        k += 1;
    }
    None
}

fn visible_rows(idx: usize) -> i16 {
    let h = s().widgets[idx].rect.h;
    let r = (h - 2) / LIST_ROW_H;
    if r < 1 {
        1
    } else {
        r
    }
}

fn ensure_visible(idx: usize) {
    let vis = visible_rows(idx);
    let sel = s().widgets[idx].sel;
    if sel < 0 {
        return;
    }
    let mut top = s().widgets[idx].top;
    if sel < top {
        top = sel;
    } else if sel >= top + vis {
        top = sel - vis + 1;
    }
    if top < 0 {
        top = 0;
    }
    s().widgets[idx].top = top;
}

/* ================================================================ */
/*  描画 (C1 の G API の上。ステートレス)                             */
/* ================================================================ */

/// 木を 1 本描く。`clip` は処理中の `Paint` 矩形 (基底クリップと同じ)。
pub fn draw_tree(surface: SurfaceId, root: u16, focus: u16, win_focused: bool, clip: Rect) {
    if root == GUI_NONE {
        return;
    }
    draw_node(surface, root as usize - 1, focus, win_focused, clip);
}

fn draw_node(surface: SurfaceId, idx: usize, focus: u16, win_focused: bool, clip: Rect) {
    let (kind, r, hidden) = {
        let w = &s().widgets[idx];
        (w.kind, w.rect, w.has(WFL_HIDDEN))
    };
    if hidden || r.intersect(&clip).is_empty() {
        return;
    }
    if is_container(kind) {
        let mut c = s().widgets[idx].first_child;
        while c != GUI_NONE {
            let ci = c as usize - 1;
            draw_node(surface, ci, focus, win_focused, clip);
            c = s().widgets[ci].next_sibling;
        }
        return;
    }
    let focused = win_focused && focus == (idx as u16) + 1;
    match kind {
        WK_LABEL => draw_label(surface, idx),
        WK_BUTTON => draw_button(surface, idx, focused),
        WK_CHECKBOX => draw_checkbox(surface, idx, focused),
        WK_TEXTBOX => draw_textbox(surface, idx, focused),
        WK_TEXTAREA => draw_textarea(surface, idx, focused),
        WK_LISTBOX => draw_listbox(surface, idx, focused),
        _ => {}
    }
}

#[inline]
fn fg_of(idx: usize) -> u8 {
    if s().widgets[idx].has(WFL_DISABLED) {
        GUI_COLOR_DISABLED
    } else {
        GUI_COLOR_TEXT
    }
}

fn draw_label(surface: SurfaceId, idx: usize) {
    let r = s().widgets[idx].rect;
    let fg = fg_of(idx);
    let y = r.y + (r.h - CELL_H) / 2;
    let txt = s().widgets[idx].text;
    let n = s().widgets[idx].text_len as usize;
    crate::draw::text(
        surface,
        r.x as i32,
        y as i32,
        &txt[..n],
        Style::new(fg, GUI_COLOR_WINDOW),
    );
}

/// 立体枠 (LIGHT = 左上、SHADOW = 右下)。押下時は入れ替える。
fn bevel(surface: SurfaceId, r: Rect, pressed: bool) {
    let (tl, br) = if pressed {
        (GUI_COLOR_SHADOW, GUI_COLOR_LIGHT)
    } else {
        (GUI_COLOR_LIGHT, GUI_COLOR_SHADOW)
    };
    crate::draw::hline(surface, r.x as i32, r.y as i32, r.w as i32, Style::pen(tl));
    crate::draw::vline(surface, r.x as i32, r.y as i32, r.h as i32, Style::pen(tl));
    crate::draw::hline(
        surface,
        r.x as i32,
        (r.y + r.h - 1) as i32,
        r.w as i32,
        Style::pen(br),
    );
    crate::draw::vline(
        surface,
        (r.x + r.w - 1) as i32,
        r.y as i32,
        r.h as i32,
        Style::pen(br),
    );
}

fn focus_ring(surface: SurfaceId, r: Rect) {
    let inner = Rect::new(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
    if inner.is_empty() {
        return;
    }
    crate::draw::draw_rect(
        surface,
        inner,
        Style::pen(GUI_COLOR_TEXT).with_flags(GUI_STYLE_DOTTED),
    );
}

fn draw_button(surface: SurfaceId, idx: usize, focused: bool) {
    let (r, pressed) = {
        let w = &s().widgets[idx];
        (w.rect, w.has(WFL_PRESSED))
    };
    crate::draw::fill_rect(surface, r, Style::new(GUI_COLOR_TEXT, GUI_COLOR_FACE));
    bevel(surface, r, pressed);
    let txt = s().widgets[idx].text;
    let n = s().widgets[idx].text_len as usize;
    let (tw, _) = crate::draw::measure_text(&txt[..n]);
    let off = if pressed { 1 } else { 0 };
    let tx = r.x as i32 + (r.w as i32 - tw) / 2 + off;
    let ty = r.y as i32 + (r.h as i32 - CELL_H as i32) / 2 + off;
    crate::draw::text(
        surface,
        tx,
        ty,
        &txt[..n],
        Style::new(fg_of(idx), GUI_COLOR_FACE),
    );
    if focused {
        focus_ring(surface, r);
    }
}

fn draw_checkbox(surface: SurfaceId, idx: usize, focused: bool) {
    let (r, checked) = {
        let w = &s().widgets[idx];
        (w.rect, w.has(WFL_CHECKED))
    };
    crate::draw::fill_rect(surface, r, Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW));
    let by = r.y + (r.h - CHECK_BOX) / 2;
    let box_r = Rect::new(r.x, by, CHECK_BOX, CHECK_BOX);
    crate::draw::fill_rect(surface, box_r, Style::new(GUI_COLOR_TEXT, GUI_COLOR_EDIT_BG));
    bevel(surface, box_r, true);
    if checked {
        let fg = fg_of(idx);
        crate::draw::line(
            surface,
            (r.x + 3) as i32,
            (by + 7) as i32,
            (r.x + 6) as i32,
            (by + 10) as i32,
            Style::pen(fg),
        );
        crate::draw::line(
            surface,
            (r.x + 6) as i32,
            (by + 10) as i32,
            (r.x + 11) as i32,
            (by + 3) as i32,
            Style::pen(fg),
        );
        crate::draw::line(
            surface,
            (r.x + 3) as i32,
            (by + 8) as i32,
            (r.x + 6) as i32,
            (by + 11) as i32,
            Style::pen(fg),
        );
        crate::draw::line(
            surface,
            (r.x + 6) as i32,
            (by + 11) as i32,
            (r.x + 11) as i32,
            (by + 4) as i32,
            Style::pen(fg),
        );
    }
    let txt = s().widgets[idx].text;
    let n = s().widgets[idx].text_len as usize;
    let ty = r.y + (r.h - CELL_H) / 2;
    crate::draw::text(
        surface,
        (r.x + CHECK_BOX + 4) as i32,
        ty as i32,
        &txt[..n],
        Style::new(fg_of(idx), GUI_COLOR_WINDOW),
    );
    if focused {
        focus_ring(surface, r);
    }
}

fn draw_textbox(surface: SurfaceId, idx: usize, focused: bool) {
    let r = s().widgets[idx].rect;
    crate::draw::fill_rect(surface, r, Style::new(GUI_COLOR_TEXT, GUI_COLOR_EDIT_BG));
    bevel(surface, r, true);
    let txt = s().widgets[idx].text;
    let n = s().widgets[idx].text_len as usize;
    let ty = r.y + (r.h - CELL_H) / 2;
    crate::draw::text(
        surface,
        (r.x + 3) as i32,
        ty as i32,
        &txt[..n],
        Style::new(fg_of(idx), GUI_COLOR_EDIT_BG),
    );
    if focused {
        let caret = s().widgets[idx].caret as usize;
        let c = if caret > n { n } else { caret };
        let (cw, _) = crate::draw::measure_text(&txt[..c]);
        crate::draw::vline(
            surface,
            (r.x + 3) as i32 + cw,
            (r.y + 2) as i32,
            (r.h - 4) as i32,
            Style::pen(GUI_COLOR_HIGHLIGHT),
        );
    }
}

fn draw_listbox(surface: SurfaceId, idx: usize, focused: bool) {
    let (r, top, sel, count) = {
        let w = &s().widgets[idx];
        (w.rect, w.top, w.sel, w.item_count)
    };
    crate::draw::fill_rect(surface, r, Style::new(GUI_COLOR_TEXT, GUI_COLOR_EDIT_BG));
    bevel(surface, r, true);
    let vis = visible_rows(idx);
    let mut row = 0i16;
    while row < vis {
        let index = top + row;
        if index >= count {
            break;
        }
        let ry = r.y + 1 + row * LIST_ROW_H;
        let selected = index == sel;
        let (bg, fg) = if selected {
            (GUI_COLOR_SEL_BG, GUI_COLOR_SEL_TEXT)
        } else {
            (GUI_COLOR_EDIT_BG, fg_of(idx))
        };
        if selected {
            crate::draw::fill_rect(
                surface,
                Rect::new(r.x + 1, ry, r.w - 2, LIST_ROW_H),
                Style::new(fg, bg),
            );
        }
        if let Some(k) = item_slot(idx, index) {
            let t = s().items[k].text;
            let n = s().items[k].len as usize;
            crate::draw::text(
                surface,
                (r.x + 3) as i32,
                (ry + 1) as i32,
                &t[..n],
                Style::new(fg, bg),
            );
        }
        row += 1;
    }
    if focused {
        focus_ring(surface, r);
    }
}

/* ================================================================ */
/*  当たり判定とフォーカス                                            */
/* ================================================================ */

/// クライアントローカル座標にある葉ウィジェット (ラベルは対象外)。
pub fn hit(root: u16, x: i32, y: i32) -> Option<usize> {
    if root == GUI_NONE {
        return None;
    }
    hit_node(root as usize - 1, x, y)
}

fn hit_node(idx: usize, x: i32, y: i32) -> Option<usize> {
    let (kind, r, hidden) = {
        let w = &s().widgets[idx];
        (w.kind, w.rect, w.has(WFL_HIDDEN))
    };
    if hidden || !r.contains(x, y) {
        return None;
    }
    if is_container(kind) {
        let mut c = s().widgets[idx].first_child;
        while c != GUI_NONE {
            let ci = c as usize - 1;
            if let Some(h) = hit_node(ci, x, y) {
                return Some(h);
            }
            c = s().widgets[ci].next_sibling;
        }
        return None;
    }
    if kind == WK_LABEL || s().widgets[idx].has(WFL_DISABLED) {
        return None;
    }
    Some(idx)
}

/// 木を作成順 (index 順) に走査して focusable を集める。
fn focus_order(root: u16, out: &mut [usize]) -> usize {
    let mut n = 0;
    if root == GUI_NONE {
        return 0;
    }
    collect_focusable(root as usize - 1, out, &mut n);
    n
}

fn collect_focusable(idx: usize, out: &mut [usize], n: &mut usize) {
    let (kind, hidden, disabled) = {
        let w = &s().widgets[idx];
        (w.kind, w.has(WFL_HIDDEN), w.has(WFL_DISABLED))
    };
    if hidden {
        return;
    }
    if is_container(kind) {
        let mut c = s().widgets[idx].first_child;
        while c != GUI_NONE {
            let ci = c as usize - 1;
            collect_focusable(ci, out, n);
            c = s().widgets[ci].next_sibling;
        }
        return;
    }
    if is_focusable(kind) && !disabled && *n < out.len() {
        out[*n] = idx;
        *n += 1;
    }
}

/// Tab / Shift+Tab。返り値: 新しいフォーカス (index + 1)。
pub fn advance_focus(root: u16, cur: u16, backward: bool) -> u16 {
    let mut order = [0usize; GUI_MAX_WIDGETS];
    let n = focus_order(root, &mut order);
    if n == 0 {
        return GUI_NONE;
    }
    let mut pos = usize::MAX;
    let mut k = 0;
    while k < n {
        if order[k] + 1 == cur as usize {
            pos = k;
            break;
        }
        k += 1;
    }
    let next = if pos == usize::MAX {
        0
    } else if backward {
        (pos + n - 1) % n
    } else {
        (pos + 1) % n
    };
    (order[next] as u16) + 1
}

/// 最初の focusable (窓を作った直後のフォーカス)。
pub fn first_focusable(root: u16) -> u16 {
    let mut order = [0usize; GUI_MAX_WIDGETS];
    let n = focus_order(root, &mut order);
    if n == 0 {
        GUI_NONE
    } else {
        (order[0] as u16) + 1
    }
}

/* ================================================================ */
/*  入力 → ウィジェットイベントの合成 (契約 U6)                       */
/* ================================================================ */

/// マウス押下 (クライアントローカル)。フォーカス移動と押下状態の更新。
pub fn on_button_down(win: usize, x: i32, y: i32) -> WidgetOut {
    let mut out = WidgetOut::EMPTY;
    let root = s().windows[win].root;
    let hitw = hit(root, x, y);
    let idx = match hitw {
        Some(i) => i,
        None => return out,
    };
    if s().windows[win].focus != (idx as u16) + 1 {
        set_focus_slot(win, (idx as u16) + 1, &mut out);
    }
    let kind = s().widgets[idx].kind;
    match kind {
        WK_BUTTON | WK_CHECKBOX => {
            s().widgets[idx].flags |= WFL_PRESSED;
            s().windows[win].armed = (idx as u16) + 1;
            invalidate(idx);
        }
        WK_LISTBOX => {
            let r = s().widgets[idx].rect;
            let rel = y - (r.y as i32 + 1);
            if rel >= 0 {
                let index = s().widgets[idx].top as i32 + rel / LIST_ROW_H as i32;
                if index >= 0 && index < s().widgets[idx].item_count as i32 {
                    if s().widgets[idx].sel as i32 != index {
                        s().widgets[idx].sel = index as i16;
                        ensure_visible(idx);
                        invalidate(idx);
                        out.push(WEV_SELECT, id_of(idx), index);
                    }
                }
            }
        }
        WK_TEXTBOX => {
            let r = s().widgets[idx].rect;
            let rel = x - (r.x as i32 + 3);
            let caret = caret_from_x(idx, rel);
            s().widgets[idx].caret = caret;
            invalidate(idx);
            send_text_cursor(idx);
        }
        _ => {}
    }
    out
}

/// マウス解放。アーム中なら CLICK / TOGGLED を合成する。
pub fn on_button_up(win: usize, x: i32, y: i32) -> WidgetOut {
    let mut out = WidgetOut::EMPTY;
    let armed = s().windows[win].armed;
    if armed == GUI_NONE {
        return out;
    }
    s().windows[win].armed = GUI_NONE;
    let idx = armed as usize - 1;
    if !s().widgets[idx].used {
        return out;
    }
    s().widgets[idx].flags &= !WFL_PRESSED;
    invalidate(idx);
    let r = s().widgets[idx].rect;
    if !r.contains(x, y) {
        return out; /* 外で離した → キャンセル */
    }
    match s().widgets[idx].kind {
        WK_BUTTON => out.push(WEV_CLICK, id_of(idx), 0),
        WK_CHECKBOX => {
            let on = !s().widgets[idx].has(WFL_CHECKED);
            if on {
                s().widgets[idx].flags |= WFL_CHECKED;
            } else {
                s().widgets[idx].flags &= !WFL_CHECKED;
            }
            out.push(WEV_TOGGLED, id_of(idx), on as i32);
        }
        _ => {}
    }
    out
}

/// マウス移動。アーム中のボタンから外れたら押下表示を戻す。
pub fn on_pointer(win: usize, x: i32, y: i32) {
    let armed = s().windows[win].armed;
    if armed == GUI_NONE {
        return;
    }
    let idx = armed as usize - 1;
    if !s().widgets[idx].used {
        return;
    }
    let inside = s().widgets[idx].rect.contains(x, y);
    let pressed = s().widgets[idx].has(WFL_PRESSED);
    if inside != pressed {
        if inside {
            s().widgets[idx].flags |= WFL_PRESSED;
        } else {
            s().widgets[idx].flags &= !WFL_PRESSED;
        }
        invalidate(idx);
    }
}

/// キー押下 (契約 U2a: `scan` は PC-98 スキャンコード)。
/// 文字の挿入は `Text` の担当 ([`on_text`])。ここは編集キーとショートカット。
pub fn on_key(win: usize, scan: u8, mods: u8) -> WidgetOut {
    let mut out = WidgetOut::EMPTY;
    let root = s().windows[win].root;

    if scan == SCAN_TAB {
        let cur = s().windows[win].focus;
        let next = advance_focus(root, cur, (mods & MOD_SHIFT) != 0);
        if next != cur {
            set_focus_slot(win, next, &mut out);
        }
        return out;
    }

    let f = s().windows[win].focus;
    if f == GUI_NONE {
        return out;
    }
    let idx = f as usize - 1;
    match s().widgets[idx].kind {
        WK_TEXTBOX => key_textbox(idx, scan, &mut out),
        WK_LISTBOX => key_listbox(idx, scan, &mut out),
        WK_CHECKBOX => {
            if scan == SCAN_SPACE || scan == SCAN_RETURN {
                let on = !s().widgets[idx].has(WFL_CHECKED);
                if on {
                    s().widgets[idx].flags |= WFL_CHECKED;
                } else {
                    s().widgets[idx].flags &= !WFL_CHECKED;
                }
                invalidate(idx);
                out.push(WEV_TOGGLED, id_of(idx), on as i32);
            }
        }
        WK_BUTTON => {
            if scan == SCAN_SPACE || scan == SCAN_RETURN {
                out.push(WEV_CLICK, id_of(idx), 0);
            }
        }
        _ => {}
    }
    out
}

/// 確定文字列の挿入 (契約 U2a: FEP オンなら確定文字列がここに来る)。
pub fn on_text(win: usize, utf8: &[u8]) -> WidgetOut {
    let mut out = WidgetOut::EMPTY;
    let f = s().windows[win].focus;
    if f == GUI_NONE {
        return out;
    }
    let idx = f as usize - 1;
    if s().widgets[idx].kind == WK_TEXTAREA {
        /* textarea は**本文を持たない**ので自分では入れない。確定文字列を
         * 溜めて TEXT_CHANGED を出し、アプリが `textarea_take_input` で
         * 引き取って自分の本文へ入れる (票 §2 の「部品に持たせない」)。 */
        if ta_stash_input(idx, utf8) {
            out.push(WEV_TEXT_CHANGED, id_of(idx), 0);
        }
        return out;
    }
    if s().widgets[idx].kind != WK_TEXTBOX {
        return out;
    }
    let mut changed = false;
    let mut i = 0;
    while i < utf8.len() {
        if utf8[i] == 0 {
            break;
        }
        let n = utf8_seq_len(utf8[i]);
        if i + n > utf8.len() {
            break;
        }
        if tb_insert(idx, &utf8[i..i + n]) {
            changed = true;
        }
        i += n;
    }
    if changed {
        invalidate(idx);
        send_text_cursor(idx);
        out.push(WEV_TEXT_CHANGED, id_of(idx), 0);
    }
    out
}

/* ---- フォーカスの張り替え ---- */

fn set_focus_slot(win: usize, next: u16, out: &mut WidgetOut) {
    let old = s().windows[win].focus;
    if old != GUI_NONE && s().widgets[old as usize - 1].used {
        invalidate(old as usize - 1);
    }
    s().windows[win].focus = next;
    if next != GUI_NONE {
        let idx = next as usize - 1;
        invalidate(idx);
        out.push(WEV_FOCUS, id_of(idx), 0);
        let k = s().widgets[idx].kind;
        if k == WK_TEXTBOX || k == WK_TEXTAREA {
            send_text_cursor(idx);
        }
    }
}

/// フォーカスをウィジェットへ移す (アプリから)。
///
/// **`on_widget_focus` は必ず来る。** 入力から合成したフォーカス移動 (TAB /
/// クリック) は `WidgetOut` でループへ返るが、ここはハンドラの外からも呼ばれる
/// ので返す先が無い。溜めておき、ループが配る ([`take_pending_focus`])。
///
/// これを捨てていたのが穴 H13 — アプリが `on_widget_focus` だけで
/// フォーカスを追うと、自分で移した後は**二度と編集面に戻ったと分からない**。
/// 文字は `Text` 経由で入るので、**カーソルキーだけが死ぬ**という形で出た。
pub fn set_focus(id: WidgetId) -> GuiResult<()> {
    let idx = resolve(id).ok_or(GuiErr::STALE)?;
    let win_id = s().widgets[idx].window;
    let win = s().win_slot(win_id).ok_or(GuiErr::STALE)?;
    let mut out = WidgetOut::EMPTY;
    set_focus_slot(win, (idx as u16) + 1, &mut out);
    let mut i = 0;
    while i < out.n {
        if out.evs[i].kind == WEV_FOCUS {
            s().pending_focus = out.evs[i].widget.raw();
        }
        i += 1;
    }
    Ok(())
}

/// 溜めてあるアプリ発フォーカス通知を 1 件取り出す (無ければ `NULL`)。
/// 配るのはループ ([`crate::app`]) の仕事。
pub fn take_pending_focus() -> WidgetId {
    let id = s().pending_focus;
    s().pending_focus = 0;
    WidgetId(id)
}

/// フォーカス中のウィジェット (**窓スロットの添字**で引く)。
///
/// アプリが持つのは WindowId なので、アプリからは [`focused_of`] を使う。
pub fn focused(win: usize) -> WidgetId {
    let f = s().windows[win].focus;
    if f == GUI_NONE {
        WidgetId::NULL
    } else {
        id_of(f as usize - 1)
    }
}

/// 窓 (**WindowId**) でフォーカス中のウィジェット。無効な窓なら `NULL`。
///
/// [`focused`] は窓スロットの添字を取るが、**アプリが添字を引く口は無い**。
/// そのためアプリはフォーカスを `on_widget_focus` で自分で追うしかなく、
/// それが穴 H13 を踏む前提になっていた。**アプリはこちらを使う。**
pub fn focused_of(window: u32) -> WidgetId {
    match s().win_slot(window) {
        Some(i) => focused(i),
        None => WidgetId::NULL,
    }
}

/// テキストボックスのキャレット位置を WM へ知らせる (契約 U2a、FEP の候補窓)。
/// 窓が `Focus{in}` を受けたとき、フォーカス中のウィジェットが textbox なら
/// キャレット位置を WM へ知らせ直す (FEP の `[あ]` / 候補窓の原点。窓生成直後は
/// まだ送っていないので、これが無いと画面左下に出る — G3 で実測)。
pub(crate) fn report_text_cursor(win: usize) {
    let f = s().windows[win].focus;
    if f != GUI_NONE {
        let idx = f as usize - 1;
        let k = s().widgets[idx].kind;
        if s().widgets[idx].used && (k == WK_TEXTBOX || k == WK_TEXTAREA) {
            send_text_cursor(idx);
        }
    }
}

fn send_text_cursor(idx: usize) {
    if s().widgets[idx].kind == WK_TEXTAREA {
        ta_send_text_cursor(idx);
        return;
    }
    let (win_id, r, caret) = {
        let w = &s().widgets[idx];
        (w.window, w.rect, w.caret as usize)
    };
    if win_id == 0 {
        return;
    }
    let txt = s().widgets[idx].text;
    let n = s().widgets[idx].text_len as usize;
    let c = if caret > n { n } else { caret };
    let (cw, _) = crate::draw::measure_text(&txt[..c]);
    let x = r.x as i32 + 3 + cw;
    let y = r.y as i32 + (r.h as i32 - CELL_H as i32) / 2;
    let _ = client::win_set_text_cursor(win_id, x as i16, y as i16, true);
}

/* ---- textbox の編集 (契約 U6: BS / DEL / 矢印 / HOME) ---- */

fn key_textbox(idx: usize, scan: u8, out: &mut WidgetOut) {
    let mut changed = false;
    let mut moved = false;
    match scan {
        SCAN_BS => changed = tb_backspace(idx),
        SCAN_DEL => changed = tb_delete(idx),
        SCAN_LEFT => {
            let c = s().widgets[idx].caret;
            if c > 0 {
                s().widgets[idx].caret = prev_boundary(idx, c);
                moved = true;
            }
        }
        SCAN_RIGHT => {
            let c = s().widgets[idx].caret;
            let n = s().widgets[idx].text_len as i16;
            if c < n {
                s().widgets[idx].caret = next_boundary(idx, c);
                moved = true;
            }
        }
        SCAN_HOME => {
            if s().widgets[idx].caret != 0 {
                s().widgets[idx].caret = 0;
                moved = true;
            }
        }
        _ => {}
    }
    if changed || moved {
        invalidate(idx);
        send_text_cursor(idx);
    }
    if changed {
        out.push(WEV_TEXT_CHANGED, id_of(idx), 0);
    }
}

/* 境界とバイト移動の実体は `textcore` (決裁 A1 の「共通の下請け」)。
 * ここを直に書き直すと textarea・エディタ本文と桁の数え方がずれるので、
 * **必ず textcore を通す** (受入 E10 の変異はここで RED になる)。 */

fn prev_boundary(idx: usize, caret: i16) -> i16 {
    let n = s().widgets[idx].text_len as usize;
    textcore::prev_boundary(&s().widgets[idx].text[..n], caret as usize) as i16
}

fn next_boundary(idx: usize, caret: i16) -> i16 {
    let n = s().widgets[idx].text_len as usize;
    textcore::next_boundary(&s().widgets[idx].text[..n], caret as usize) as i16
}

fn tb_insert(idx: usize, seq: &[u8]) -> bool {
    let n = s().widgets[idx].text_len as usize;
    let mut c = s().widgets[idx].caret as usize;
    if c > n {
        c = n;
    }
    let buf = &mut s().widgets[idx].text[..TEXT_CAP];
    match textcore::insert(buf, n, c, seq) {
        Some(newlen) => {
            s().widgets[idx].text_len = newlen as u8;
            s().widgets[idx].caret = (c + seq.len()) as i16;
            true
        }
        None => false,
    }
}

fn tb_remove(idx: usize, at: usize, len: usize) {
    let n = s().widgets[idx].text_len as usize;
    let buf = &mut s().widgets[idx].text[..TEXT_CAP];
    let newlen = textcore::remove(buf, n, at, len);
    s().widgets[idx].text_len = newlen as u8;
}

fn tb_backspace(idx: usize) -> bool {
    let c = s().widgets[idx].caret;
    if c <= 0 {
        return false;
    }
    let p = prev_boundary(idx, c);
    tb_remove(idx, p as usize, (c - p) as usize);
    s().widgets[idx].caret = p;
    true
}

fn tb_delete(idx: usize) -> bool {
    let c = s().widgets[idx].caret;
    let n = s().widgets[idx].text_len as i16;
    if c >= n {
        return false;
    }
    let q = next_boundary(idx, c);
    tb_remove(idx, c as usize, (q - c) as usize);
    true
}

/// クリック位置 (テキスト先頭からの px) に一番近い UTF-8 境界。
fn caret_from_x(idx: usize, rel: i32) -> i16 {
    if rel <= 0 {
        return 0;
    }
    let n = s().widgets[idx].text_len as usize;
    let txt = s().widgets[idx].text;
    let mut p = 0usize;
    while p < n {
        let step = utf8_seq_len(txt[p]);
        let q = if p + step > n { n } else { p + step };
        let (w, _) = crate::draw::measure_text(&txt[..q]);
        if w > rel {
            /* この文字の中。手前寄りなら前の境界。 */
            let (wp, _) = crate::draw::measure_text(&txt[..p]);
            if rel - wp < w - rel {
                return p as i16;
            }
            return q as i16;
        }
        p = q;
    }
    n as i16
}

/* ================================================================ */
/*  WK_TEXTAREA — 複数行の編集面 (票 TASK_EDIT_GUI §2、決裁 A1)        */
/*                                                                  */
/*  **本文はここに無い。** アプリ (エディタ) が本文を持ち、いま見えて  */
/*  いる行だけを `textarea_clear` + `textarea_add_row` で写す。        */
/*  部品がするのは                                                    */
/*    - 写された行を描く                                              */
/*    - キャレットを描き、FEP へその位置を知らせる                     */
/*    - 確定文字列を溜めてアプリへ渡す                                 */
/*  の 3 つだけ。折り返し・縦スクロール・選択範囲はアプリの領分で、     */
/*  そう切ったから 1 画面に入らないファイルでも部品の側は一定になる。   */
/* ================================================================ */

/// `widget_idx` が持つ相対 `index` 行のスロット。
fn ta_row_slot(widget_idx: usize, index: i16) -> Option<usize> {
    let st = s();
    let mut k = 0;
    while k < GUI_MAX_TEXTAREA_ROWS {
        if st.rows[k].used
            && st.rows[k].owner == (widget_idx as u16) + 1
            && st.rows[k].index == index
        {
            return Some(k);
        }
        k += 1;
    }
    None
}

/// 矩形に入る行数 (`textarea_visible_rows` の実体)。
fn ta_capacity(idx: usize) -> i16 {
    let h = s().widgets[idx].rect.h;
    let r = (h - 2) / TEXTAREA_ROW_H;
    if r < 1 {
        1
    } else {
        r
    }
}

/// 矩形に入る半角の桁数 (`textarea_columns` の実体)。
fn ta_cols(idx: usize) -> i16 {
    let w = s().widgets[idx].rect.w;
    let c = (w - 2 - EDIT_PAD_X * 2) / ANK_W;
    if c < 1 {
        1
    } else {
        c
    }
}

/// 写してある行を全部捨てる。
pub fn textarea_clear(id: WidgetId) -> GuiResult<()> {
    let i = resolve(id).ok_or(GuiErr::STALE)?;
    if s().widgets[i].kind != WK_TEXTAREA {
        return Err(GuiErr::INVAL);
    }
    let mut k = 0;
    while k < GUI_MAX_TEXTAREA_ROWS {
        if s().rows[k].used && s().rows[k].owner == (i as u16) + 1 {
            s().rows[k] = TextRow::EMPTY;
        }
        k += 1;
    }
    s().widgets[i].ta_rows = 0;
    invalidate(i);
    Ok(())
}

/// 見える行を 1 本足す (上から順に)。戻りはその相対行番号。
///
/// 入り切らない行は `GuiErr::FULL`。**黙って切り詰めない** — 呼び手が
/// [`textarea_visible_rows`] を見て渡す本数を決める。
pub fn textarea_add_row(id: WidgetId, text: &[u8]) -> GuiResult<i32> {
    let i = resolve(id).ok_or(GuiErr::STALE)?;
    if s().widgets[i].kind != WK_TEXTAREA {
        return Err(GuiErr::INVAL);
    }
    let index = s().widgets[i].ta_rows;
    let mut k = 0;
    while k < GUI_MAX_TEXTAREA_ROWS {
        if !s().rows[k].used {
            break;
        }
        k += 1;
    }
    if k >= GUI_MAX_TEXTAREA_ROWS {
        return Err(GuiErr::FULL);
    }
    /* 切るなら UTF-8 の境界で (`docs/POLICY_DEBUG.md` §4-27)。 */
    let n = crate::utf8core::utf8_truncate(text, GUI_TEXTAREA_ROW_CAP);
    s().rows[k] = TextRow::EMPTY;
    s().rows[k].used = true;
    s().rows[k].owner = (i as u16) + 1;
    s().rows[k].index = index;
    s().rows[k].len = n as u16;
    let mut j = 0;
    while j < n {
        s().rows[k].text[j] = text[j];
        j += 1;
    }
    s().widgets[i].ta_rows = index + 1;
    invalidate(i);
    Ok(index as i32)
}

/// キャレットを置く (`row` は見える範囲の先頭からの相対、`col` はその行の
/// 先頭からの**バイト数**)。`row < 0` でキャレットを消す。
pub fn textarea_set_caret(id: WidgetId, row: i32, col: i32) -> GuiResult<()> {
    let i = resolve(id).ok_or(GuiErr::STALE)?;
    if s().widgets[i].kind != WK_TEXTAREA {
        return Err(GuiErr::INVAL);
    }
    s().widgets[i].ta_caret_row = row as i16;
    s().widgets[i].ta_caret_col = if col < 0 { 0 } else { col as i16 };
    invalidate(i);
    send_text_cursor(i);
    Ok(())
}

/// 矩形に入る行数。アプリはこれを見て「見える範囲」を切り出す ([C4])。
pub fn textarea_visible_rows(id: WidgetId) -> i32 {
    match resolve(id) {
        Some(i) if s().widgets[i].kind == WK_TEXTAREA => ta_capacity(i) as i32,
        _ => 0,
    }
}

/// 矩形に入る半角の桁数。アプリはこれを折り返しの幅に使う ([C4])。
/// 全角は 2 桁を占めるので、この値は**桁**であって文字数ではない。
pub fn textarea_columns(id: WidgetId) -> i32 {
    match resolve(id) {
        Some(i) if s().widgets[i].kind == WK_TEXTAREA => ta_cols(i) as i32,
        _ => 0,
    }
}

/// 溜まった確定文字列を引き取る (引き取ったら空になる)。戻りは写したバイト数。
///
/// `out` が短ければ**何も渡さず 0**。半端に渡して UTF-8 を割るより、
/// 呼び手に十分な `out` を用意させるほうが安全 (上限は共有定数)。
pub fn textarea_take_input(id: WidgetId, out: &mut [u8]) -> usize {
    let i = match resolve(id) {
        Some(i) if s().widgets[i].kind == WK_TEXTAREA => i,
        _ => return 0,
    };
    let n = s().widgets[i].ta_input_len as usize;
    if n == 0 || out.len() < n {
        return 0;
    }
    let mut j = 0;
    while j < n {
        out[j] = s().widgets[i].ta_input[j];
        j += 1;
    }
    s().widgets[i].ta_input_len = 0;
    n
}

/// 確定文字列を溜める。溢れる分は捨てるが、**符号単位の途中では切らない**。
fn ta_stash_input(idx: usize, utf8: &[u8]) -> bool {
    let mut len = s().widgets[idx].ta_input_len as usize;
    let mut i = 0usize;
    let mut any = false;
    while i < utf8.len() {
        if utf8[i] == 0 {
            break;
        }
        let n = utf8_seq_len(utf8[i]);
        if i + n > utf8.len() || len + n > GUI_TEXTAREA_INPUT_CAP {
            break;
        }
        let mut j = 0;
        while j < n {
            s().widgets[idx].ta_input[len + j] = utf8[i + j];
            j += 1;
        }
        len += n;
        i += n;
        any = true;
    }
    s().widgets[idx].ta_input_len = len as u8;
    any
}

/// キャレット位置を WM へ (FEP の `[あ]` と候補窓の原点。契約 U2a)。
fn ta_send_text_cursor(idx: usize) {
    let (win_id, r, row, col) = {
        let w = &s().widgets[idx];
        (w.window, w.rect, w.ta_caret_row, w.ta_caret_col as usize)
    };
    if win_id == 0 || row < 0 {
        return;
    }
    let cw = match ta_row_slot(idx, row) {
        Some(k) => {
            let n = s().rows[k].len as usize;
            let c = if col > n { n } else { col };
            crate::draw::measure_text(&s().rows[k].text[..c]).0
        }
        None => 0,
    };
    let x = r.x as i32 + EDIT_PAD_X as i32 + cw;
    let y = r.y as i32 + 1 + row as i32 * TEXTAREA_ROW_H as i32;
    let _ = client::win_set_text_cursor(win_id, x as i16, y as i16, true);
}

fn draw_textarea(surface: SurfaceId, idx: usize, focused: bool) {
    let (r, nrows, crow, ccol) = {
        let w = &s().widgets[idx];
        (w.rect, w.ta_rows, w.ta_caret_row, w.ta_caret_col as usize)
    };
    crate::draw::fill_rect(surface, r, Style::new(GUI_COLOR_TEXT, GUI_COLOR_EDIT_BG));
    bevel(surface, r, true);
    let cap = ta_capacity(idx);
    let vis = if nrows < cap { nrows } else { cap };
    let mut row = 0i16;
    while row < vis {
        if let Some(k) = ta_row_slot(idx, row) {
            let n = s().rows[k].len as usize;
            let txt = s().rows[k].text;
            crate::draw::text(
                surface,
                (r.x + EDIT_PAD_X) as i32,
                (r.y + 1 + row * TEXTAREA_ROW_H) as i32,
                &txt[..n],
                Style::new(fg_of(idx), GUI_COLOR_EDIT_BG),
            );
        }
        row += 1;
    }
    if focused && crow >= 0 && crow < vis {
        let cw = match ta_row_slot(idx, crow) {
            Some(k) => {
                let n = s().rows[k].len as usize;
                let c = if ccol > n { n } else { ccol };
                crate::draw::measure_text(&s().rows[k].text[..c]).0
            }
            None => 0,
        };
        crate::draw::vline(
            surface,
            (r.x + EDIT_PAD_X) as i32 + cw,
            (r.y + 1 + crow * TEXTAREA_ROW_H) as i32,
            TEXTAREA_ROW_H as i32,
            Style::pen(GUI_COLOR_HIGHLIGHT),
        );
    }
    if focused {
        focus_ring(surface, r);
    }
}

/* ---- listbox のキー ---- */

fn key_listbox(idx: usize, scan: u8, out: &mut WidgetOut) {
    let count = s().widgets[idx].item_count;
    if count == 0 {
        return;
    }
    let vis = visible_rows(idx);
    let mut sel = s().widgets[idx].sel;
    match scan {
        SCAN_UP => sel = if sel <= 0 { 0 } else { sel - 1 },
        SCAN_DOWN => {
            sel = if sel < 0 { 0 } else { sel + 1 };
            if sel >= count {
                sel = count - 1;
            }
        }
        SCAN_ROLLUP => {
            sel = if sel < 0 { 0 } else { sel - vis };
            if sel < 0 {
                sel = 0;
            }
        }
        SCAN_ROLLDOWN => {
            sel = if sel < 0 { 0 } else { sel + vis };
            if sel >= count {
                sel = count - 1;
            }
        }
        SCAN_HOME => sel = 0,
        _ => return,
    }
    if sel != s().widgets[idx].sel {
        s().widgets[idx].sel = sel;
        ensure_visible(idx);
        invalidate(idx);
        out.push(WEV_SELECT, id_of(idx), sel as i32);
    }
}
