//! gui_demo — GUI シェル v1.1 の目視確認デモ (票 C2 作業 8、ゲート G2 のアプリ側)
//!
//! gshell (WM) の下で動く**普通のアプリ**。旧版の「アプリの中で WM を動かす」
//! 構成 (`gui_pump` / `gui_draw`) は廃止し、契約 U3 のループ + 所有型 + `row` /
//! `column` の箱レイアウトに書き換えた。
//!
//! Window 1「Widgets」: テキストボックス / チェックボックス / リストボックス /
//!   OK ボタンと、それぞれの状態を映すラベル。
//! Window 2「Help」: 操作説明。
//!
//! 確認できること (ゲート G2):
//!   - 窓 2 枚・タイトルバーのドラッグ・前面化・閉じるボタン (WM 側)
//!   - テキストボックス編集 (タイプ / ← → / BS / DEL / HOME)
//!   - チェックボックス (クリック or SPACE)
//!   - リストボックス (クリック or ↑↓ / ROLL)
//!   - Tab / Shift+Tab のフォーカス移動 (点線の枠)
//!   - 400 / 480 ラインで崩れないこと (すべて row / column)
//!
//! 起動: gshell の F1、またはシェルから `gui_demo`。終了: ESC / 窓を全部閉じる。
#![no_std]
#![no_main]

extern crate libos32gui;
extern crate os32api;

use libos32gui::widget::{self, WidgetId, SCAN_ESC};
use libos32gui::{App, SizeSpec, Ui, Window, WindowSpec};
use os32api::gui::types::Rect;
use os32api::KernelAPI;

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    if libos32gui::init(api).is_err() {
        return -1;
    }
    let mut demo = match Demo::build() {
        Ok(d) => d,
        Err(e) => return e.code(),
    };
    let _ = libos32gui::run(&mut demo);
    0
}

/* ================================================================ */
/*  アプリの状態 (窓は所有型。Drop で WM へ DESTROY)                 */
/* ================================================================ */

struct Demo {
    w1: Option<Window>,
    w2: Option<Window>,
    tb: WidgetId,
    cb: WidgetId,
    lb: WidgetId,
    btn: WidgetId,
    lbl_cb: WidgetId,
    lbl_sel: WidgetId,
    lbl_tb: WidgetId,
    lbl_ok: WidgetId,
}

impl Demo {
    fn build() -> libos32gui::GuiResult<Demo> {
        /* 画面能力を信じる (契約 G5: 640×400 / 16 色を決め打ちしない)。 */
        let info = libos32gui::gapi::screen_info();
        let sw = info.width as i32;
        let sh = info.height as i32;
        let w1w = clamp(sw / 2, 220, 330);
        let w1h = clamp(sh * 3 / 5, 150, 240);
        let w2w = clamp(sw * 2 / 5, 200, 244);
        let w2h = clamp(sh * 45 / 100, 120, 180);

        /* ---- Window 1: ウィジェット ---- */
        let w1 = Window::create(
            &WindowSpec::new(
                b"Widgets",
                Rect::new((sw / 16) as i16, (sh / 10) as i16, w1w as i16, w1h as i16),
            )
            .min_size(220, 150),
        )?;

        let root = widget::column(8, 6)?;

        let name_row = widget::row(0, 6)?;
        let lbl_name = widget::label(b"name:")?;
        let tb = widget::textbox(b"")?;
        widget::add(name_row, lbl_name, SizeSpec::Fixed(48))?;
        widget::add(name_row, tb, SizeSpec::Flex(1))?;
        widget::add(root, name_row, SizeSpec::Fixed(20))?;

        let cb = widget::checkbox(b"Enable sound", false)?;
        widget::add(root, cb, SizeSpec::Fixed(18))?;

        let lbl_cb = widget::label(b"sound: OFF")?;
        widget::add(root, lbl_cb, SizeSpec::Fixed(16))?;

        let mid = widget::row(0, 8)?;
        let lb = widget::listbox()?;
        widget::list_add(lb, b"Apple")?;
        widget::list_add(lb, b"Banana")?;
        widget::list_add(lb, b"Cherry")?;
        widget::list_add(lb, b"Durian")?;
        widget::list_add(lb, b"Elderberry")?;
        widget::list_add(lb, b"Fig")?;

        let side = widget::column(0, 6)?;
        let lbl_sel = widget::label(b"sel: Apple")?;
        let btn = widget::button(b"OK")?;
        let lbl_ok = widget::label(b"")?;
        widget::add(side, lbl_sel, SizeSpec::Fixed(16))?;
        widget::add(side, btn, SizeSpec::Fixed(26))?;
        widget::add(side, lbl_ok, SizeSpec::Flex(1))?;

        widget::add(mid, lb, SizeSpec::Flex(2))?;
        widget::add(mid, side, SizeSpec::Flex(1))?;
        widget::add(root, mid, SizeSpec::Flex(1))?;

        let lbl_tb = widget::label(b"text:")?;
        widget::add(root, lbl_tb, SizeSpec::Fixed(16))?;

        w1.set_root(root)?;

        /* ---- Window 2: ヘルプ ---- */
        let w2x = clamp(sw / 16 + w1w + 12, 0, sw - w2w - 4);
        let w2 = Window::create(&WindowSpec::new(
            b"Help",
            Rect::new(w2x as i16, (sh / 6) as i16, w2w as i16, w2h as i16),
        ))?;
        let help = widget::column(8, 4)?;
        add_help(help, b"Tab: next widget")?;
        add_help(help, b"S-Tab: prev widget")?;
        add_help(help, b"type: edit textbox")?;
        add_help(help, b"SPACE: toggle check")?;
        add_help(help, b"UP/DOWN: list select")?;
        add_help(help, b"drag title: move win")?;
        add_help(help, b"x: close   ESC: quit")?;
        w2.set_root(help)?;

        /* Window 1 を最前面・フォーカスに */
        w1.set_focus()?;

        Ok(Demo {
            w1: Some(w1),
            w2: Some(w2),
            tb,
            cb,
            lb,
            btn,
            lbl_cb,
            lbl_sel,
            lbl_tb,
            lbl_ok,
        })
    }
}

fn clamp(v: i32, lo: i32, hi: i32) -> i32 {
    if v < lo {
        lo
    } else if v > hi {
        hi
    } else {
        v
    }
}

fn add_help(parent: WidgetId, text: &[u8]) -> libos32gui::GuiResult<()> {
    let l = widget::label(text)?;
    widget::add(parent, l, SizeSpec::Fixed(18))
}

/* ================================================================ */
/*  ハンドラ (種別ごと。巨大 switch は書かない — 契約 U6)             */
/* ================================================================ */

impl App for Demo {
    fn on_click(&mut self, _ui: &mut Ui, w: WidgetId) {
        if w == self.btn {
            let _ = widget::set_text(self.lbl_ok, b"OK pressed!");
        }
    }

    fn on_toggled(&mut self, _ui: &mut Ui, w: WidgetId, on: bool) {
        if w == self.cb {
            let msg: &[u8] = if on { b"sound: ON" } else { b"sound: OFF" };
            let _ = widget::set_text(self.lbl_cb, msg);
            set_bgm(on);
        }
    }

    fn on_text_changed(&mut self, _ui: &mut Ui, w: WidgetId) {
        if w == self.tb {
            let mut content = [0u8; 48];
            let n = widget::text(self.tb, &mut content);
            let mut buf = [0u8; 64];
            let k = concat(&mut buf, b"text: ", &content[..n]);
            let _ = widget::set_text(self.lbl_tb, &buf[..k]);
        }
    }

    fn on_select(&mut self, _ui: &mut Ui, w: WidgetId, index: i32) {
        if w == self.lb {
            let mut item = [0u8; 40];
            let n = widget::list_item_text(self.lb, index, &mut item);
            let mut buf = [0u8; 64];
            let k = concat(&mut buf, b"sel: ", &item[..n]);
            let _ = widget::set_text(self.lbl_sel, &buf[..k]);
        }
    }

    /// 閉じるボタン: その窓だけ落とす (`Option::take` → `Drop` で DESTROY)。
    fn on_close(&mut self, ui: &mut Ui, window: u32) {
        if self.w1.as_ref().map(|w| w.id()) == Some(window) {
            self.w1 = None;
        } else if self.w2.as_ref().map(|w| w.id()) == Some(window) {
            self.w2 = None;
        }
        if self.w1.is_none() && self.w2.is_none() {
            ui.quit();
        }
    }

    /// ESC で終了 (契約 U2a: `scan` は PC-98 スキャンコード)。
    fn on_key(&mut self, ui: &mut Ui, _window: u32, scan: u8, _ch: u8, _mods: u8, down: bool) {
        if down && scan == SCAN_ESC {
            ui.quit();
        }
    }
}

/* ================================================================ */
/*  BGM (受入 G10 の観測手段)                                        */
/* ================================================================ */

/* `[...]` が無限ループなので、止めるまで鳴り続ける。フォーカスが外れたとき
 * に止まる / 戻ったとき鳴り直すのは WM 側 (音のフォーカス排他) の領分。 */
static BGM_MML: &[u8] = b"T120 O4 L8 [CEGE DFAF]\0";

/// チェックボックスに合わせて BGM を鳴らす / 止める。
fn set_bgm(on: bool) {
    unsafe {
        let a = os32api::api();
        if on {
            (a.snd_bgm_play)(BGM_MML.as_ptr());
        } else {
            (a.snd_bgm_stop)();
        }
    }
}

/* prefix + src を dst に詰める。返り値: 書いたバイト数。 */
fn concat(dst: &mut [u8], prefix: &[u8], src: &[u8]) -> usize {
    let mut n = 0usize;
    let mut i = 0;
    while i < prefix.len() && n < dst.len() {
        dst[n] = prefix[i];
        n += 1;
        i += 1;
    }
    let mut j = 0;
    while j < src.len() && src[j] != 0 && n < dst.len() {
        dst[n] = src[j];
        n += 1;
        j += 1;
    }
    n
}
