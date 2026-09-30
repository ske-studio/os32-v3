//! app.rs — U3 のループ (GetMessage の代替、入れ子なし)。票 C2 作業 3。
//!
//! ```text
//! loop {
//!     n = OP_POLL                  // リングから一括で取り出す
//!     for ev in events[..n] { handle(ev) }   // 状態更新 + invalidate だけ
//!     paint_damaged()              // Paint を受けた窓を描く
//!     commit_all()                 // 1 周 1 回
//!     OP_WAIT(next_timer)          // 何も無ければ sys_halt で待つ
//! }
//! ```
//!
//! - ハンドラの中から `OP_WAIT` を呼ばない (`client::wait` の `debug_assert`)。
//! - `Quit` を受けたら速やかに戻る。
//! - 1 周の `gui_call` は `POLL` + `COMMIT` + `WAIT` + 状態変更だけ (契約 P)。

use core::ffi::c_void;

use crate::client::{self, GuiResult};
use crate::uistate::{s, GUI_NONE};
use crate::widget::{self, WEV_CLICK, WEV_FOCUS, WEV_SELECT, WEV_TEXT_CHANGED, WEV_TOGGLED};
use crate::window;
use os32api::gui::proto::{
    GuiEvent, GUI_COLOR_TEXT, GUI_COLOR_WINDOW, GUI_EV_BUTTON, GUI_EV_CLOSE, GUI_EV_CONFIGURE,
    GUI_EV_FOCUS, GUI_EV_KEY, GUI_EV_MODAL, GUI_EV_PAINT, GUI_EV_PALETTE, GUI_EV_POINTER,
    GUI_EV_QUIT, GUI_EV_TEXT, GUI_EV_TIMER, GUI_RING_CAPACITY,
};
use os32api::gui::stub::AppVTable;
use os32api::gui::types::{Rect, Style};

/* drivers/mouse.h の MOUSE_BTN_*。`GuiEvtButton.button` に載る値 (契約 D4)。 */
const MOUSE_BTN_LEFT: u8 = 0x01;

/* アプリ発フォーカス通知を 1 か所で配る回数の上限 ([C4]: 数値を散らさない)。
 * `on_widget_focus` の中で更に `set_focus` を呼ぶ作りを許すが、取り合いで
 * 周回が止まらないように止める。 */
const FOCUS_DRAIN_MAX: u32 = 4;

/* ================================================================ */
/*  Ui / App                                                        */
/*                                                                  */
/*  型の正典は `os32api::gui::stub` — 共有ライブラリ (票 C3) の両側が  */
/*  同じ形を見る必要がある。ループ (ここ) と `Ui` の状態はライブラリの  */
/*  `.data`/`.bss` にあり、ハンドラの実体はアプリ側にある。境界は       */
/*  `AppVTable` (C ABI) 1 枚。                                       */
/* ================================================================ */

pub use os32api::gui::stub::{App, Ui};

/* ================================================================ */
/*  ハンドラ表の包み — ループからはふつうのメソッド呼びに見せる        */
/* ================================================================ */

pub(crate) struct VApp {
    vt: *const AppVTable,
    this: *mut c_void,
}

#[allow(dead_code)]
impl VApp {
    #[inline]
    fn vt(&self) -> &AppVTable {
        unsafe { &*self.vt }
    }
    #[inline]
    fn on_click(&self, ui: &mut Ui, w: widget::WidgetId) {
        (self.vt().on_click)(self.this, ui, w.raw())
    }
    #[inline]
    fn on_toggled(&self, ui: &mut Ui, w: widget::WidgetId, on: bool) {
        (self.vt().on_toggled)(self.this, ui, w.raw(), on as u32)
    }
    #[inline]
    fn on_text_changed(&self, ui: &mut Ui, w: widget::WidgetId) {
        (self.vt().on_text_changed)(self.this, ui, w.raw())
    }
    #[inline]
    fn on_select(&self, ui: &mut Ui, w: widget::WidgetId, index: i32) {
        (self.vt().on_select)(self.this, ui, w.raw(), index)
    }
    #[inline]
    fn on_widget_focus(&self, ui: &mut Ui, w: widget::WidgetId) {
        (self.vt().on_widget_focus)(self.this, ui, w.raw())
    }
    #[inline]
    fn on_raw(&self, ui: &mut Ui, ev: &GuiEvent) {
        (self.vt().on_raw)(self.this, ui, ev as *const GuiEvent)
    }
    #[inline]
    fn on_close(&self, ui: &mut Ui, window: u32) {
        (self.vt().on_close)(self.this, ui, window)
    }
    #[inline]
    fn on_focus(&self, ui: &mut Ui, window: u32, focused: bool) {
        (self.vt().on_focus)(self.this, ui, window, focused as u32)
    }
    #[inline]
    fn on_key(&self, ui: &mut Ui, window: u32, scan: u8, ch: u8, mods: u8, down: bool) {
        (self.vt().on_key)(
            self.this,
            ui,
            window,
            scan as u32,
            ch as u32,
            mods as u32,
            down as u32,
        )
    }
    #[inline]
    fn on_timer(&self, ui: &mut Ui, window: u32, id: u8) {
        (self.vt().on_timer)(self.this, ui, window, id as u32)
    }
    #[inline]
    fn on_configure(&self, ui: &mut Ui, window: u32) {
        (self.vt().on_configure)(self.this, ui, window)
    }
    #[inline]
    fn on_palette(&self, ui: &mut Ui, window: u32, active: bool) {
        (self.vt().on_palette)(self.this, ui, window, active as u32)
    }
    #[inline]
    fn on_modal(&self, ui: &mut Ui, dialog: u16, result: i16) {
        (self.vt().on_modal)(self.this, ui, dialog as u32, result as i32)
    }
    #[inline]
    fn on_quit(&self, ui: &mut Ui, reason: u8) {
        (self.vt().on_quit)(self.this, ui, reason as u32)
    }
    #[inline]
    fn on_overflow(&self, ui: &mut Ui, dropped: u16) {
        (self.vt().on_overflow)(self.this, ui, dropped as u32)
    }
    #[inline]
    fn on_paint(&self, ui: &mut Ui, window: u32, surface: os32api::gui::types::SurfaceId, rect: Rect) {
        (self.vt().on_paint)(self.this, ui, window, surface.raw(), rect)
    }
    #[inline]
    fn after_commit(&self, ui: &mut Ui) {
        (self.vt().after_commit)(self.this, ui)
    }
}

/* ================================================================ */
/*  U3 ループ                                                        */
/* ================================================================ */

/// 1 アプリ 1 本のイベントループ (ジャンプ表 `E_RUN` の実体)。
/// `Quit` / 全ウィンドウ消滅 / `Ui::quit` で戻る。
pub fn run_vt(vt: *const AppVTable, this: *mut c_void, ui: &mut Ui) -> GuiResult<()> {
    if vt.is_null() {
        return Err(client::GuiErr::INVAL);
    }
    let app = VApp { vt, this };
    let app = &app;
    let zero = GuiEvent { kind: 0, sub: 0, serial: 0, window: 0, payload: [0; 8] };
    let mut buf = [zero; GUI_RING_CAPACITY];

    /* 窓を組む間に `widget::set_focus` を呼んでいることがある (穴 H13)。 */
    client::enter_handler();
    drain_pending_focus(app, ui);
    client::leave_handler();

    loop {
        let p = client::poll(&mut buf)?;

        /* 取りこぼしは受け取った周だけ「入力状態を未知」にする (契約 T3)。 */
        s().input_unknown = p.overflow;
        if p.overflow {
            client::dbg_print_num(b"[gui] input OVERFLOW, dropped =", p.dropped as i32);
            client::enter_handler();
            app.on_overflow(ui, p.dropped);
            client::leave_handler();
        }

        /* (1) handle: 状態更新 + invalidate だけ。ここで描かない。 */
        client::enter_handler();
        let mut i = 0;
        while i < p.count {
            dispatch(app, ui, &buf[i]);
            /* ハンドラの中で `widget::set_focus` を呼んだぶんを、**次の 1 件を
             * 配る前に**知らせる (穴 H13)。同じ周に打鍵が続くとき、古い
             * フォーカスで判断させないため。 */
            drain_pending_focus(app, ui);
            i += 1;
        }
        client::leave_handler();

        /* (1b) 1 周分に溜まった損傷をまとめて申告する (契約 P: gui_call を減らす)。 */
        flush_damage();

        /* (2) Paint を受けた窓を描く。 */
        paint_damaged(app, ui);

        /* (3) commit は 1 周 1 回 (契約 P / G4)。 */
        client::commit(0)?;
        client::enter_handler();
        app.after_commit(ui);
        client::leave_handler();

        if s().quit || window::count() == 0 {
            break;
        }

        /* (4) 何も無ければ sys_halt で待つ。`after_commit` が出した損傷も先に流す。 */
        flush_damage();
        client::wait(ui.timeout_ticks)?;
    }
    Ok(())
}

/* ================================================================ */
/*  1 件の配送 (契約 T3 / U2)                                        */
/* ================================================================ */

/// アプリが自分で移したフォーカスを `on_widget_focus` で配る (穴 H13)。
///
/// ハンドラの中で更に `set_focus` を呼べるので、**往復に上限を置く** —
/// 2 つのウィジェットが互いにフォーカスを取り合っても周回が止まらない。
fn drain_pending_focus(app: &VApp, ui: &mut Ui) {
    let mut guard = 0;
    while guard < FOCUS_DRAIN_MAX {
        let w = widget::take_pending_focus();
        if w == widget::WidgetId::NULL {
            return;
        }
        app.on_widget_focus(ui, w);
        guard += 1;
    }
}

/// 溜まった損傷を `OP_INVALIDATE` で送る (窓あたり最大 8 本)。
pub fn flush_damage() {
    let n = s().damage.len;
    let mut i = 0;
    while i < n {
        let d = s().damage.items[i];
        let _ = client::invalidate(d.window, d.rect);
        i += 1;
    }
    s().damage.clear();
}

#[inline]
fn to_rect(r: os32api::gui::proto::GuiRect16) -> Rect {
    Rect::new(r.x, r.y, r.w, r.h)
}

fn dispatch(app: &VApp, ui: &mut Ui, ev: &GuiEvent) {
    /* generation が自分の表と一致しないイベントは捨てる (契約 U2)。 */
    let slot = s().win_slot(ev.window);
    if slot.is_none() && ev.kind != GUI_EV_QUIT {
        return;
    }
    let win = ev.window;
    app.on_raw(ui, ev);

    match ev.kind {
        GUI_EV_PAINT => {
            s().paint.push(win, to_rect(ev.rect()));
        }
        GUI_EV_CONFIGURE => {
            window::on_configure(win, to_rect(ev.rect()));
            app.on_configure(ui, win);
        }
        GUI_EV_CLOSE => app.on_close(ui, win),
        GUI_EV_FOCUS => {
            let i = slot.unwrap();
            s().windows[i].focused = ev.sub != 0;
            /* フォーカスリングの見た目が変わるのはフォーカス中のウィジェットだけ。 */
            let f = s().windows[i].focus;
            if f != GUI_NONE {
                widget::invalidate(f as usize - 1);
            }
            if ev.sub != 0 {
                widget::report_text_cursor(i);
            }
            app.on_focus(ui, win, ev.sub != 0);
        }
        GUI_EV_KEY => {
            let k = ev.key();
            if ev.sub != 0 {
                let out = widget::on_key(slot.unwrap(), k.scan, k.mods);
                emit(app, ui, &out);
            }
            app.on_key(ui, win, k.scan, k.ch, k.mods, ev.sub != 0);
        }
        GUI_EV_TEXT => {
            let t = ev.text();
            let len = (ev.sub & 0x7F) as usize;
            let n = if len > 8 { 8 } else { len };
            let out = widget::on_text(slot.unwrap(), &t.utf8[..n]);
            emit(app, ui, &out);
        }
        GUI_EV_POINTER => {
            let p = ev.pointer();
            s().ptr_x = p.x;
            s().ptr_y = p.y;
            widget::on_pointer(slot.unwrap(), p.x as i32, p.y as i32);
        }
        GUI_EV_BUTTON => {
            let b = ev.button();
            /* ウィジェット操作は**左ボタンだけ**。WM は契約 D4 で右ボタンも
             * 前面窓のクライアントへ配るので、種別を見ないと右クリックで
             * on_click やチェック切替が起きる。右ボタンが要るアプリは
             * `on_raw` (この match より前に必ず呼ばれる) で受ける。 */
            if b.button == MOUSE_BTN_LEFT {
                let i = slot.unwrap();
                let out = if ev.sub != 0 {
                    widget::on_button_down(i, b.x as i32, b.y as i32)
                } else {
                    widget::on_button_up(i, b.x as i32, b.y as i32)
                };
                emit(app, ui, &out);
            }
        }
        GUI_EV_TIMER => {
            /* 単発は WM が消す (契約 U5)。クライアント側の台帳は不要。 */
            app.on_timer(ui, win, ev.sub);
        }
        GUI_EV_MODAL => {
            let m = ev.modal();
            app.on_modal(ui, m.dialog, m.result);
        }
        GUI_EV_QUIT => app.on_quit(ui, ev.sub),
        GUI_EV_PALETTE => app.on_palette(ui, win, ev.sub != 0),
        _ => {}
    }
}

/// 合成したウィジェットイベントを種別ごとのハンドラへ渡す。
fn emit(app: &VApp, ui: &mut Ui, out: &widget::WidgetOut) {
    let evs = out.as_slice();
    let mut i = 0;
    while i < evs.len() {
        let e = evs[i];
        match e.kind {
            WEV_CLICK => app.on_click(ui, e.widget),
            WEV_TOGGLED => app.on_toggled(ui, e.widget, e.value != 0),
            WEV_TEXT_CHANGED => app.on_text_changed(ui, e.widget),
            WEV_SELECT => app.on_select(ui, e.widget, e.value),
            WEV_FOCUS => app.on_widget_focus(ui, e.widget),
            _ => {}
        }
        i += 1;
    }
}

/* ================================================================ */
/*  描画 (契約 G2: 基底クリップ = 処理中の Paint 矩形)                */
/* ================================================================ */

fn paint_damaged(app: &VApp, ui: &mut Ui) {
    let n = s().paint.len;
    let mut i = 0;
    while i < n {
        let req = s().paint.items[i];
        i += 1;
        let slot = match s().win_slot(req.window) {
            Some(k) => k,
            None => continue, /* 破棄済みの窓宛 (契約 U2) */
        };
        let surface = s().windows[slot].surface;
        if surface.is_null() {
            continue;
        }
        /* 基底クリップを Paint 矩形に固定する。この外へは 1 画素も出ない。 */
        if crate::clip::set_base_clip(surface, req.rect) < 0 {
            continue;
        }
        /* クライアント面の下地 (WM は枠しか描かない)。 */
        crate::draw::fill_rect(
            surface,
            req.rect,
            Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW),
        );
        let root = s().windows[slot].root;
        let focus = s().windows[slot].focus;
        let focused = s().windows[slot].focused;
        widget::draw_tree(surface, root, focus, focused, req.rect);
        app.on_paint(ui, req.window, surface, req.rect);
        crate::clip::clear_base_clip();
    }
    s().paint.clear();
}
