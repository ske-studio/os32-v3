//! about — 「About OS32」ダイアログ (/usr/bin/about.bin、ROADMAP v1.4 の About dialog)。
//!
//! Start → Programs の about.bin (/usr/bin/*.bin の一覧) から起動する普通の
//! libos32gui アプリ。窓 1 枚にラベルを並べ、OK ボタンで閉じる。Start の root
//! メニューに専用の項目は足していない (gshell と台本 `gui_gate.py` の `ROW_*` が動くため)。
//!
//! 表示する行は実行中カーネルの sys_ver_info から一度だけ取得する。
//! 文言・順序は kernel/ver_format.c が持ち、About は kind ごとに区切る。
//! CUI との一致・ABI・窓の寸法は tools/tests/test_about_info.py で確認する。
//!
//! 閉じ方: RETURN / ESC / OK ボタン / 閉じるボタン / GRPH+f･4 (WM 側)。
#![no_std]
#![no_main]

extern crate libos32gui;
extern crate os32api;

pub mod info;

use info::VerInfo;
use libos32gui::widget::{self, WidgetId, SCAN_ESC, SCAN_RETURN};
use libos32gui::{App, SizeSpec, Ui, Window, WindowSpec};
use os32api::gui::types::Rect;
use os32api::KernelAPI;

/* 窓の寸法 (px)。ホスト試験が読んで、libos32gui の配置の規則で積み上げを計算する。 */
const WIN_W_MAX: i32 = 440;
const WIN_H_MAX: i32 = 368;
const PAD: i16 = 10;
const GAP: i16 = 1;
const TITLE_H: i16 = 20;
const LINE_H: i16 = 16;
const SEP_H: i16 = 4;
const BAR_H: i16 = 24;
const OK_W: i16 = 72;

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    if libos32gui::init(api).is_err() {
        return -1;
    }
    let mut about = match About::build() {
        Ok(a) => a,
        Err(e) => return e.code(),
    };
    let _ = libos32gui::run(&mut about);
    0
}

struct About {
    win: Option<Window>,
    ok: WidgetId,
}

/// 新しいユーザーランドは新カーネル専用 (OS32X の実効 min_api)。
fn collect(report: &mut VerInfo) -> i32 {
    let a = unsafe { os32api::api() };
    unsafe { (a.sys_ver_info)(report as *mut VerInfo as *mut u8,
                             core::mem::size_of::<VerInfo>() as u32) }
}

impl About {
    fn build() -> libos32gui::GuiResult<About> {
        let mut report = VerInfo::new();
        let rc = collect(&mut report);
        if rc != 0 {
            return Err(libos32gui::GuiErr(rc));
        }
        let lines = report.rows();
        let n = lines.len();

        /* 最大16行、kind の区切り3本、OK の段を400ライン画面にも収める。
         * 行間は1px。ABI の予約20行とは別に、実際の count だけ描画する。 */
        let si = libos32gui::gapi::screen_info();
        let sw = si.width as i32;
        let sh = si.height as i32;
        let ww = clamp(sw - 16, 240, WIN_W_MAX);
        let wh = clamp(sh - 40, 200, WIN_H_MAX);
        let wx = (sw - ww) / 2;
        let wy = clamp((sh - 24 - wh) / 2, 0, sh);

        let win = Window::create(&WindowSpec::new(
            b"About OS32",
            Rect::new(wx as i16, wy as i16, ww as i16, wh as i16),
        ))?;

        /* 空の row は min_h=0。label を区切りに使うと16pxに広がる。 */
        let root = widget::column(PAD, GAP)?;
        let mut i = 0;
        while i < n {
            if i > 0 && lines[i].group() != lines[i - 1].group() {
                let sep = widget::row(0, 0)?;
                widget::add(root, sep, SizeSpec::Fixed(SEP_H))?;
            }
            let l = widget::label(lines[i].as_bytes())?;
            widget::add(root, l, SizeSpec::Fixed(if i == 0 { TITLE_H } else { LINE_H }))?;
            i += 1;
        }
        let spacer = widget::row(0, 0)?;
        widget::add(root, spacer, SizeSpec::Flex(1))?;

        let bar = widget::row(0, 0)?;
        let pad = widget::label(b"")?;
        let ok = widget::button(b"OK")?;
        widget::add(bar, pad, SizeSpec::Flex(1))?;
        widget::add(bar, ok, SizeSpec::Fixed(OK_W))?;
        widget::add(root, bar, SizeSpec::Fixed(BAR_H))?;

        win.set_root(root)?;
        win.set_focus()?;
        widget::set_focus(ok)?;

        Ok(About { win: Some(win), ok })
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

impl App for About {
    fn on_click(&mut self, ui: &mut Ui, w: WidgetId) {
        if w == self.ok {
            ui.quit();
        }
    }

    /// 閉じるボタン (と WM の GRPH+f･4) — 窓を落として終わる。
    fn on_close(&mut self, ui: &mut Ui, _window: u32) {
        self.win = None;
        ui.quit();
    }

    /// RETURN / ESC で閉じる (OK にフォーカスが無くても効く)。
    fn on_key(&mut self, ui: &mut Ui, _window: u32, scan: u8, _ch: u8, _mods: u8, down: bool) {
        if down && (scan == SCAN_ESC || scan == SCAN_RETURN) {
            ui.quit();
        }
    }
}
