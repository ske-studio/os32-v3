//! about — 「About OS32」ダイアログ (/usr/bin/about.bin、ROADMAP v1.4 の About dialog)。
//!
//! Start → Programs の about.bin (/usr/bin/*.bin の一覧) から起動する普通の
//! libos32gui アプリ。窓 1 枚にラベルを並べ、OK ボタンで閉じる。Start の root
//! メニューに専用の項目は足していない (gshell と台本 `gui_gate.py` の `ROW_*` が動くため)。
//!
//! **出すものは `ver` の出力と同じ** (ユーザー指示 2026-09-29): 版の行、機器の 7 行、
//! API、Build、(KAPI v65 以上で `boot_image_info` が通れば) Commit と Image CRC。
//! 値は `ver` と同じ出どころ (include/config.h の `SYS_VERSION` をコンパイル時に切り出す、
//! `KernelAPI.version`、`sys_get_build_info`、`boot_image_info`)。KAPI は足していない。
//! 文字列の組み立ては [`info`]、`ver` との突き合わせはホスト試験
//! `tools/tests/test_about_info.py` (実物の cmd_base.c を組んで比べる)。
//!
//! 閉じ方: RETURN / ESC / OK ボタン / 閉じるボタン / GRPH+f･4 (WM 側)。
#![no_std]
#![no_main]

extern crate libos32gui;
extern crate os32api;

pub mod info;

use info::{BootImage, Line, NLINES_MAX, VER_HW};
use libos32gui::widget::{self, WidgetId, SCAN_ESC, SCAN_RETURN};
use libos32gui::{App, SizeSpec, Ui, Window, WindowSpec};
use os32api::gui::types::Rect;
use os32api::KernelAPI;

/// 版の唯一の定義 (include/config.h) から `SYS_VERSION` をコンパイル時に切り出す。
const SYS_VERSION: &[u8] = info::sys_version(include_bytes!("../../../../include/config.h"));

/// `BootImageInfo` (os32_kapi_shared.h、40 バイト、並びは固定) の写し。
#[repr(C)]
struct BootImageInfo {
    image_crc: u32,
    image_size: u32,
    crc_valid: u8,
    source: u8,
    reserved: u16,
    commit: [u8; 24],
    reserved2: u32,
}
const _: () = assert!(core::mem::size_of::<BootImageInfo>() == 40);

/* 窓の寸法 (px)。ホスト試験が読んで、libos32gui の配置の規則で積み上げを計算する。 */
const WIN_W_MAX: i32 = 440;
const WIN_H_MAX: i32 = 312;
const PAD: i16 = 10;
const GAP: i16 = 2;
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

/// `ver` と同じ行を組み立て、行の数を返す。
fn collect(lines: &mut [Line; NLINES_MAX]) -> usize {
    let a = unsafe { os32api::api() };
    let kver = a.version;

    let mut build = [0u8; 32];
    unsafe { (a.sys_get_build_info)(build.as_mut_ptr(), build.len() as i32) };
    build[build.len() - 1] = 0;

    let mut bi = BootImageInfo {
        image_crc: 0,
        image_size: 0,
        crc_valid: 0,
        source: 0,
        reserved: 0,
        commit: [0; 24],
        reserved2: 0,
    };
    /* v65 未満のカーネルには boot_image_info の枠が無いので呼ばない (`ver` と同じ)。 */
    let bi_ok = kver >= info::KAPI_BOOT_IMAGE_INFO
        && info::boot_shown(kver, unsafe {
            (a.boot_image_info)(&mut bi as *mut BootImageInfo as *mut u8)
        });
    bi.commit[bi.commit.len() - 1] = 0;

    let boot = BootImage {
        crc_valid: bi.crc_valid != 0,
        crc: bi.image_crc,
        size: bi.image_size,
        source: bi.source,
        commit: &bi.commit,
    };
    info::ver_lines(lines, SYS_VERSION, kver, &build, if bi_ok { Some(&boot) } else { None })
}

impl About {
    fn build() -> libos32gui::GuiResult<About> {
        const EMPTY: Line = Line::new();
        let mut lines: [Line; NLINES_MAX] = [EMPTY; NLINES_MAX];
        let n = collect(&mut lines);

        /* 画面能力を信じる (契約 G5: 640×400 を決め打ちしない)。
         * 幅は ver の最長の行 (Image CRC の 50 桁) が入る WIN_W_MAX を上限に。
         * 高さの上限 WIN_H_MAX は 12 行 + 区切り 2 本 + OK の段が入る値
         * (寸法はホスト試験 test_about_info.py が libos32gui の下限込みで計算する)。 */
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

        /* 見出し (版の行) / 機器の 7 行 / 識別の行 (API・Build・Commit・Image CRC) の
         * 3 段に分け、段の間に SEP_H の区切りを置く。
         * 区切りと伸びる余白は**空の row** (下限 min_h = 0)。label は下限が 16px
         * (libos32gui の CELL_H) なので、Fixed(4) を渡しても 16px になり、
         * 2026-09-29 のゲスト確認で OK ボタンが窓の下端で切れた。 */
        let root = widget::column(PAD, GAP)?;
        let mut i = 0;
        while i < n {
            if i == 1 || i == 1 + VER_HW.len() {
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
