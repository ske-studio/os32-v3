//! lease_test — パレットのリース (契約 G8) の検証用プログラム。票 C2 作業 9。
//!
//! フォーカスを得たら 14 色 (index 1〜6 と 8〜15) をリースし、自前パレットの絵を
//! 描く。`Palette{active:false}` を受けたら**代替色 (システム色)** で描き直す。
//!
//! **現状 (2026-09-06)**: W1 の WM は `OP_LEASE_PALETTE` を `OS32_ERR_NOSYS` で
//! 返す (W2 待ち)。したがってこのプログラムは「リース要求 → NOSYS を表示 →
//! 代替色の絵」までを確認する。W2 が入ったら同じ実行で自前色に変わるはず。
//!
//! 操作: ESC で終了。SPACE でリースの張り直しを試す。
#![no_std]
#![no_main]

extern crate libos32gui;
extern crate os32api;

use libos32gui::gapi as g;
use libos32gui::widget::{SCAN_ESC, SCAN_SPACE};
use libos32gui::{App, Ui, Window, WindowSpec};
use os32api::gui::proto::{
    GuiRgb, GUI_COLOR_TEXT, GUI_COLOR_WINDOW, GUI_SYSTEM_PALETTE,
};
use os32api::gui::types::{Rect, Style, SurfaceId};
use os32api::KernelAPI;

/* リースしたい 14 色 (index 1〜6 と 8〜15)。不可侵は 0 (黒) と 7 (白)。 */
const LEASE_LO_FIRST: u16 = 1;
const LEASE_LO_COUNT: usize = 6; /* 1〜6 */
const LEASE_HI_FIRST: u16 = 8;
const LEASE_HI_COUNT: usize = 8; /* 8〜15 */

/// 自前パレット (青→赤のグラデーション 14 段)。RGB 各 0〜15。
static MY_PALETTE: [GuiRgb; 14] = [
    GuiRgb { r: 0, g: 0, b: 15 },
    GuiRgb { r: 2, g: 0, b: 14 },
    GuiRgb { r: 4, g: 0, b: 13 },
    GuiRgb { r: 6, g: 0, b: 12 },
    GuiRgb { r: 8, g: 0, b: 11 },
    GuiRgb { r: 10, g: 0, b: 10 },
    GuiRgb { r: 12, g: 0, b: 8 },
    GuiRgb { r: 14, g: 0, b: 6 },
    GuiRgb { r: 15, g: 2, b: 4 },
    GuiRgb { r: 15, g: 5, b: 2 },
    GuiRgb { r: 15, g: 8, b: 0 },
    GuiRgb { r: 15, g: 11, b: 0 },
    GuiRgb { r: 15, g: 13, b: 4 },
    GuiRgb { r: 15, g: 15, b: 8 },
];

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    if libos32gui::init(api).is_err() {
        return -1;
    }
    let w = match Window::create(&WindowSpec::new(b"lease_test", Rect::new(80, 60, 400, 240))) {
        Ok(w) => w,
        Err(e) => return e.code(),
    };
    let _ = w.set_focus();
    let mut app = Lease { win: Some(w), active: false, last: 0, tried: false };
    /* 1 枚しか窓が無いと `Focus{in}` が来ない (W1 の `set_focus` は既に最前面
     * なら何も出さない) ので、起動時に 1 回試す。 */
    app.try_lease();
    let _ = libos32gui::run(&mut app);
    0
}

struct Lease {
    win: Option<Window>,
    /// `Palette{active}` の最新値。true = 自前パレットが入っている。
    active: bool,
    /// 直近の `lease_palette` の戻り値 (負なら `OS32_ERR_*`)。
    last: i32,
    tried: bool,
}

impl Lease {
    /// 14 色を 2 回に分けて借りる (1〜6 と 8〜15。7 と 0 は不可侵)。
    fn try_lease(&mut self) {
        let info = g::screen_info();
        /* 16 色バックエンドは lease_mask のビットで貸せる index を示す (契約 G8)。 */
        if info.lease_mask != 0 && (info.lease_mask & 0x0002) == 0 {
            self.last = os32api::gui::proto::OS32_ERR_INVAL;
            self.tried = true;
            return;
        }
        let r1 = libos32gui::client::lease_palette(LEASE_LO_FIRST, &MY_PALETTE[..LEASE_LO_COUNT]);
        let r2 = libos32gui::client::lease_palette(
            LEASE_HI_FIRST,
            &MY_PALETTE[LEASE_LO_COUNT..LEASE_LO_COUNT + LEASE_HI_COUNT],
        );
        self.last = match (r1, r2) {
            (Ok(a), Ok(_)) => a,
            (Err(e), _) => e.code(),
            (_, Err(e)) => e.code(),
        };
        self.tried = true;
        libos32gui::client::dbg_print_num(b"[lease_test] lease_palette ->", self.last);
    }

    fn invalidate(&self) {
        if let Some(w) = self.win.as_ref() {
            let _ = w.invalidate_all();
        }
    }
}

impl App for Lease {
    fn on_focus(&mut self, _ui: &mut Ui, _window: u32, focused: bool) {
        if focused && !self.tried {
            self.try_lease();
            self.invalidate();
        }
    }

    /// リースの有効 / 無効は WM が知らせる (契約 G8)。代替色で描き直す。
    fn on_palette(&mut self, _ui: &mut Ui, _window: u32, active: bool) {
        self.active = active;
        self.invalidate();
    }

    fn on_key(&mut self, ui: &mut Ui, _window: u32, scan: u8, _ch: u8, _mods: u8, down: bool) {
        if !down {
            return;
        }
        if scan == SCAN_ESC {
            ui.quit();
        } else if scan == SCAN_SPACE {
            self.tried = false;
            self.try_lease();
            self.invalidate();
        }
    }

    fn on_close(&mut self, ui: &mut Ui, _window: u32) {
        self.win = None;
        ui.quit();
    }

    /// 14 段の帯。リース中は自前パレット、そうでなければ**代替色**で近似する。
    fn on_paint(&mut self, _ui: &mut Ui, _window: u32, surface: SurfaceId, _rect: Rect) {
        let (cw, ch) = match self.win.as_ref() {
            Some(w) => w.client_size(),
            None => return,
        };

        let title: &[u8] = if self.active {
            b"leased palette (14 colors)"
        } else {
            b"system palette (fallback)"
        };
        g::text(surface, 6, 4, title, Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW));

        let mut msg = [0u8; 48];
        let n = fmt_num(&mut msg, b"lease_palette -> ", self.last);
        g::text(surface, 6, 22, &msg[..n], Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW));

        /* 帯 14 本。リース中は index をそのまま (中身が自前色)、代替時は
         * 自前色に一番近いシステム色を選ぶ。 */
        let top = 44i32;
        let bar_h = (ch as i32 - top - 24).max(8);
        let bw = (cw as i32 - 12) / 14;
        let mut i = 0usize;
        while i < 14 {
            let idx = lease_index(i);
            let color = if self.active { idx } else { nearest_system(MY_PALETTE[i]) };
            let x = 6 + (i as i32) * bw;
            g::fill_rect(
                surface,
                Rect::new(x as i16, top as i16, bw as i16 - 1, bar_h as i16),
                Style::new(GUI_COLOR_TEXT, color),
            );
            i += 1;
        }

        g::text(
            surface,
            6,
            (ch as i32 - 18) as i32,
            b"SPACE: re-lease   ESC: quit",
            Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW),
        );
    }
}

/// i 番目 (0..13) のリース先 index (1〜6, 8〜15)。
fn lease_index(i: usize) -> u8 {
    if i < LEASE_LO_COUNT {
        (LEASE_LO_FIRST as usize + i) as u8
    } else {
        (LEASE_HI_FIRST as usize + (i - LEASE_LO_COUNT)) as u8
    }
}

/// 自前色に一番近いシステム色 (代替描画。G8 の「気にするアプリは描き直す」)。
fn nearest_system(c: GuiRgb) -> u8 {
    let mut best = 0u8;
    let mut best_d = i32::MAX;
    let mut i = 0usize;
    while i < 16 {
        let p = GUI_SYSTEM_PALETTE[i];
        let dr = p.r as i32 - c.r as i32;
        let dg = p.g as i32 - c.g as i32;
        let db = p.b as i32 - c.b as i32;
        let d = dr * dr + dg * dg + db * db;
        if d < best_d {
            best_d = d;
            best = i as u8;
        }
        i += 1;
    }
    best
}

/// 「文字列 + 符号つき 10 進」を組み立てる。返り値: バイト数。
fn fmt_num(dst: &mut [u8], prefix: &[u8], v: i32) -> usize {
    let mut n = 0usize;
    let mut i = 0;
    while i < prefix.len() && n < dst.len() {
        dst[n] = prefix[i];
        n += 1;
        i += 1;
    }
    let neg = v < 0;
    let mut u = if neg { (-(v as i64)) as u32 } else { v as u32 };
    if neg && n < dst.len() {
        dst[n] = b'-';
        n += 1;
    }
    let mut tmp = [0u8; 12];
    let mut k = 0usize;
    if u == 0 {
        tmp[0] = b'0';
        k = 1;
    }
    while u > 0 && k < tmp.len() {
        tmp[k] = b'0' + (u % 10) as u8;
        u /= 10;
        k += 1;
    }
    let mut j = k;
    while j > 0 && n < dst.len() {
        j -= 1;
        dst[n] = tmp[j];
        n += 1;
    }
    n
}
