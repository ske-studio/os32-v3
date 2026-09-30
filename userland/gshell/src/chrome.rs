//! chrome.rs — ウィンドウの装飾 (枠・タイトルバー・閉じるボタン) とドラッグ枠の
//! 描画 (契約 U1 / R2 / G6 / G8)。**クライアント面の内側は描かない** (アプリの領分)。
//!
//! 色は G6 の役割名 (`GUI_COLOR_*`) で参照する。gshell が起動時に
//! `GUI_SYSTEM_PALETTE` を `gfx_set_palette` で入れているので index = 役割。
//!
//! **2 色クローム (第 2 モード、票 W2 の B-3)**: 14 色リース中はシステム色が
//! アプリのものに置き換わっているので、WM は不可侵の 2 色 (`TEXT` = 0 と
//! `WINDOW` = 7) とパターンだけで描く (契約 G8):
//!
//! | 部位 | 通常 | 2 色 |
//! |---|---|---|
//! | 文字・枠 | `TEXT` | `TEXT` |
//! | 面 | `FACE` / `WINDOW` | `WINDOW` |
//! | 影・無効面 | `SHADOW` | `DITHER50` (50% 市松) |
//! | フォーカス | `HIGHLIGHT` | `DOTTED` (点線) |
//! | アクティブタイトル | `TITLE_ACTIVE` 地 + `TITLE_TEXT` 字 | 黒地 + 白字 |
//! | 非アクティブタイトル | `TITLE_INACTIVE` 地 | 白地 + 黒字 |
//! | デスクトップ | `DESKTOP` | 市松 ([`crate::desktop`]) |
//!
//! ウィンドウの外 (他のウィンドウ・デスクトップ) の色崩れは許容する
//! (ユーザ決定 2026-09-04)。

use crate::wm::{Rect, Win};
use os32api::gfx;
use os32api::gui::proto::{
    GUI_COLOR_CLOSE, GUI_COLOR_HIGHLIGHT, GUI_COLOR_LIGHT, GUI_COLOR_SHADOW, GUI_COLOR_TEXT,
    GUI_COLOR_TITLE_ACTIVE, GUI_COLOR_TITLE_INACTIVE, GUI_COLOR_TITLE_TEXT, GUI_COLOR_WINDOW,
};

/* デスクトップの背景は desktop.rs (背景 + 手引き) が持つ。 */

/* ================================================================ */
/*  クリップ (契約 G4)                                               */
/* ================================================================ */
/*
 * WM はクライアント面を持たない (契約 v13 CONTRACTS)。前面窓のクライアント面へ
 * 1px でも書くと、**その窓が自分で Paint を出すまで消えない** (WM には消す手段が
 * 無い)。だからクロームの描画は必ず「その窓の可視な枠領域」で切る。
 * 切らずに描いていたのが W-3 (2026-09-11): 背面窓の右辺・下辺が前面の
 * File Manager のリスト面を縦横に貫いて残った。
 */

/// 実質クリップ無し (タスクバー / メニュー / モーダルのような最前面 UI 用)。
/// 画面より十分大きい矩形。オーバーフローしない範囲に収める。
const NO_CLIP: Rect = Rect {
    x: -(1 << 20),
    y: -(1 << 20),
    w: 1 << 21,
    h: 1 << 21,
};

/// `clip` が矩形 `r` を完全に含むか。
#[inline]
fn covers(clip: Rect, r: Rect) -> bool {
    !r.is_empty()
        && clip.x <= r.x
        && clip.y <= r.y
        && clip.right() >= r.right()
        && clip.bottom() >= r.bottom()
}

/// クリップ付きの塗り。
fn fill_in(clip: Rect, x: i32, y: i32, w: i32, h: i32, c: u8) {
    let r = Rect::new(x, y, w, h).intersect(&clip);
    if r.is_empty() {
        return;
    }
    unsafe { gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, c) };
}

/// クリップ付きの水平線 / 垂直線 / 1px 枠。線は `gfx_hline` / `gfx_vline` の
/// ままにする (プレーン構成では 1px 幅の `gfx_fill_rect` より桁違いに速い)。
fn hline_in(clip: Rect, x: i32, y: i32, w: i32, c: u8) {
    let r = Rect::new(x, y, w, 1).intersect(&clip);
    if r.is_empty() {
        return;
    }
    unsafe { gfx::gfx_hline(r.x, r.y, r.w, c) };
}
fn vline_in(clip: Rect, x: i32, y: i32, h: i32, c: u8) {
    let r = Rect::new(x, y, 1, h).intersect(&clip);
    if r.is_empty() {
        return;
    }
    unsafe { gfx::gfx_vline(r.x, r.y, r.h, c) };
}
fn rect_in(clip: Rect, x: i32, y: i32, w: i32, h: i32, c: u8) {
    if w <= 0 || h <= 0 {
        return;
    }
    hline_in(clip, x, y, w, c);
    hline_in(clip, x, y + h - 1, w, c);
    vline_in(clip, x, y, h, c);
    vline_in(clip, x + w - 1, y, h, c);
}

/// クリップ付きの直線 (閉じるボタンの ×)。`gfx_line` にクリップが無いので、
/// 外接矩形が `clip` に収まらないときだけ Bresenham をこちらで回して画素ごとに
/// 落とす。収まるなら `gfx_line` のまま (dirty 登録が 1 回で済む)。
fn line_in(clip: Rect, x0: i32, y0: i32, x1: i32, y1: i32, c: u8) {
    let bx = if x0 < x1 { x0 } else { x1 };
    let by = if y0 < y1 { y0 } else { y1 };
    let bw = if x0 < x1 { x1 - x0 } else { x0 - x1 } + 1;
    let bh = if y0 < y1 { y1 - y0 } else { y0 - y1 } + 1;
    if covers(clip, Rect::new(bx, by, bw, bh)) {
        unsafe { gfx::gfx_line(x0, y0, x1, y1, c) };
        return;
    }
    line_pixels(clip, x0, y0, x1, y1, c);
}

fn line_pixels(clip: Rect, mut x: i32, mut y: i32, x1: i32, y1: i32, c: u8) {
    let dx = if x1 > x { x1 - x } else { x - x1 };
    let sx = if x < x1 { 1 } else { -1 };
    let dy = -if y1 > y { y1 - y } else { y - y1 };
    let sy = if y < y1 { 1 } else { -1 };
    let mut e = dx + dy;
    loop {
        if clip.contains(x, y) {
            unsafe { gfx::gfx_pixel(x, y, c) };
        }
        if x == x1 && y == y1 {
            break;
        }
        let e2 = 2 * e;
        if e2 >= dy {
            e += dy;
            x += sx;
        }
        if e2 <= dx {
            e += dx;
            y += sy;
        }
    }
}

/* ================================================================ */
/*  パターン (Style.flags の DITHER50 / DOTTED に対応)               */
/* ================================================================ */

/// 50% 市松で塗る (`GUI_STYLE_DITHER50`)。地色は塗らない (透過)。
/// 小さな面 (影・無効面) 向け。広い面は [`crate::desktop`] の粗い市松を使う。
pub fn dither50(x: i32, y: i32, w: i32, h: i32, fg: u8) {
    dither50_in(NO_CLIP, x, y, w, h, fg);
}

fn dither50_in(clip: Rect, x: i32, y: i32, w: i32, h: i32, fg: u8) {
    if w <= 0 || h <= 0 {
        return;
    }
    let mut yy = 0;
    while yy < h {
        let mut xx = (yy & 1) as i32;
        while xx < w {
            if clip.contains(x + xx, y + yy) {
                unsafe { gfx::gfx_pixel(x + xx, y + yy, fg) };
            }
            xx += 2;
        }
        yy += 1;
    }
}

/// 点線の 1px 矩形 (`GUI_STYLE_DOTTED`)。フォーカスリング用。
pub fn dotted_rect(x: i32, y: i32, w: i32, h: i32, fg: u8) {
    dotted_rect_in(NO_CLIP, x, y, w, h, fg);
}

fn dotted_rect_in(clip: Rect, x: i32, y: i32, w: i32, h: i32, fg: u8) {
    if w <= 0 || h <= 0 {
        return;
    }
    let mut i = 0;
    while i < w {
        if (i & 1) == 0 {
            if clip.contains(x + i, y) {
                unsafe { gfx::gfx_pixel(x + i, y, fg) };
            }
            if clip.contains(x + i, y + h - 1) {
                unsafe { gfx::gfx_pixel(x + i, y + h - 1, fg) };
            }
        }
        i += 1;
    }
    let mut j = 0;
    while j < h {
        if (j & 1) == 0 {
            if clip.contains(x, y + j) {
                unsafe { gfx::gfx_pixel(x, y + j, fg) };
            }
            if clip.contains(x + w - 1, y + j) {
                unsafe { gfx::gfx_pixel(x + w - 1, y + j, fg) };
            }
        }
        j += 1;
    }
}

/* ================================================================ */
/*  クリップ付きのタイトル文字列                                      */
/* ================================================================ */

/// UTF-8 先頭バイトから文字の長さ (壊れていれば 1)。
fn utf8_len(b: u8) -> usize {
    if b < 0x80 {
        1
    } else if b >= 0xF0 {
        4
    } else if b >= 0xE0 {
        3
    } else if b >= 0xC0 {
        2
    } else {
        1
    }
}

/// UTF-8 1 文字を復号する (長さ, コードポイント)。壊れていれば 1 バイト分を U+FFFD。
fn utf8_next(s: &[u8], i: usize) -> (usize, u32) {
    let b = s[i];
    let n = utf8_len(b);
    if n == 1 {
        return (1, b as u32);
    }
    if i + n > s.len() {
        return (1, 0xFFFD);
    }
    let mut cp = (b as u32) & (0x7F >> n);
    let mut k = 1;
    while k < n {
        let c = s[i + k];
        if (c & 0xC0) != 0x80 {
            return (1, 0xFFFD);
        }
        cp = (cp << 6) | ((c as u32) & 0x3F);
        k += 1;
    }
    (n, cp)
}

/// `clip` の内側にだけ NUL 終端の UTF-8 を描く。
///
/// `kcg_draw_utf8` にクリップが無いので **1 文字ずつ**呼び、セル
/// (半角 8 / 全角 16 × 16px) が `clip` に**丸ごと入るときだけ**描く。
/// はみ出す文字は落とす — 前面窓のクライアント面へ半分だけ書くより、
/// その文字を描かない方が安全 (WM は書いた分を消せない)。
/// 送り幅は `kcg_draw_utf8` と同じ規則 (`unicode_to_ank` が 0 でなければ半角)。
fn draw_text_in(clip: Rect, x: i32, y: i32, s: &[u8], fg: u8, bg: u8) {
    let mut cx = x;
    let mut i = 0;
    while i < s.len() && s[i] != 0 {
        let (n, cp) = utf8_next(s, i);
        i += n;
        if cp == 0xFEFF || cp < 0x20 {
            continue; /* 制御文字は送らない (kcg と同じ) */
        }
        let cw = if unsafe { crate::ffi::unicode_to_ank(cp) } != 0 {
            8
        } else {
            16
        };
        if covers(clip, Rect::new(cx, y, cw, 16)) {
            /* 1 文字 + NUL の私有バッファ (最長 4 バイト)。 */
            let mut one = [0u8; 8];
            let mut k = 0;
            while k < n && k < 4 {
                one[k] = s[i - n + k];
                k += 1;
            }
            unsafe { gfx::kcg_draw_utf8(cx, y, one.as_ptr(), fg, bg) };
        }
        cx += cw;
    }
}

/* ================================================================ */
/*  ウィンドウの装飾                                                 */
/* ================================================================ */

/// ウィンドウの装飾を描く (バックバッファ)。
/// `active` = 最前面 (フォーカス)、`mono` = 14 色リース中の 2 色モード。
///
/// `clip` = **この窓の枠を描いてよい画面矩形**。呼び出し側
/// ([`crate::wm::composite_rect`]) が「外形 − 前面窓の外形」の 1 断片を渡す。
/// ここを切らないと背面窓の 1px の枠線が前面窓のクライアント面に残る
/// (W-3、2026-09-11)。WM はクライアント面を持たないので自力で消せない。
pub fn draw_window_chrome(win: &Win, active: bool, mono: bool, clip: Rect) {
    if !win.visible || clip.is_empty() {
        return;
    }
    unsafe {
        /* 立体枠 (2px)。外周 = 影 / ハイライトで簡易ベベル。 */
        if win.has_border() {
            /* 外枠 (黒) */
            rect_in(clip, win.x, win.y, win.w, win.h, GUI_COLOR_TEXT);
            if mono {
                /* 影は市松、ハイライトは白。 */
                hline_in(clip, win.x + 1, win.y + 1, win.w - 2, GUI_COLOR_WINDOW);
                vline_in(clip, win.x + 1, win.y + 1, win.h - 2, GUI_COLOR_WINDOW);
                dither50_in(clip, win.x + 1, win.y + win.h - 2, win.w - 2, 1, GUI_COLOR_TEXT);
                dither50_in(clip, win.x + win.w - 2, win.y + 1, 1, win.h - 2, GUI_COLOR_TEXT);
            } else {
                /* 内側ベベル: 上/左 = LIGHT、下/右 = SHADOW */
                hline_in(clip, win.x + 1, win.y + 1, win.w - 2, GUI_COLOR_LIGHT);
                vline_in(clip, win.x + 1, win.y + 1, win.h - 2, GUI_COLOR_LIGHT);
                hline_in(clip, win.x + 1, win.y + win.h - 2, win.w - 2, GUI_COLOR_SHADOW);
                vline_in(clip, win.x + win.w - 2, win.y + 1, win.h - 2, GUI_COLOR_SHADOW);
            }
        }

        /* タイトルバー */
        let tb = win.titlebar_rect();
        let (tcol, ttxt) = if mono {
            /* アクティブ = 黒地に白文字、非アクティブ = 白地に黒文字。 */
            if active {
                (GUI_COLOR_TEXT, GUI_COLOR_WINDOW)
            } else {
                (GUI_COLOR_WINDOW, GUI_COLOR_TEXT)
            }
        } else if active {
            (GUI_COLOR_TITLE_ACTIVE, GUI_COLOR_TITLE_TEXT)
        } else {
            (GUI_COLOR_TITLE_INACTIVE, GUI_COLOR_TITLE_TEXT)
        };
        fill_in(clip, tb.x, tb.y, tb.w, tb.h, tcol);
        gfx::kcg_set_scale(1);
        /* title は create/set_title で必ず NUL 終端されている。 */
        draw_text_in(clip, tb.x + 4, tb.y + 1, &win.title, ttxt, tcol);

        /* 閉じるボタン */
        if win.has_close() {
            let cr = win.close_rect();
            let face = if mono { GUI_COLOR_WINDOW } else { GUI_COLOR_CLOSE };
            let mark = if mono { GUI_COLOR_TEXT } else { GUI_COLOR_WINDOW };
            fill_in(clip, cr.x, cr.y, cr.w, cr.h, face);
            rect_in(clip, cr.x, cr.y, cr.w, cr.h, GUI_COLOR_TEXT);
            line_in(clip, cr.x + 3, cr.y + 3, cr.x + cr.w - 4, cr.y + cr.h - 4, mark);
            line_in(clip, cr.x + cr.w - 4, cr.y + 3, cr.x + 3, cr.y + cr.h - 4, mark);
        }
    }

    /* フォーカスの点線 (2 色モードでは色でなく形で示す)。 */
    if mono && active && win.has_border() {
        dotted_rect_in(clip, win.x + 2, win.y + 2, win.w - 4, win.h - 4, GUI_COLOR_TEXT);
    }
}

/// ドラッグ枠の 1px アウトラインを描く (バックバッファ)。
/// 消去は下地 (デスクトップ + クローム) の再合成で行う (wm.rs の compositor)。
/// 2 色モードでは色を使えないので**点線**で描く (契約 G8 の DOTTED)。
pub fn draw_drag_outline(x: i32, y: i32, w: i32, h: i32, mono: bool) {
    if w <= 0 || h <= 0 {
        return;
    }
    if mono {
        dotted_rect(x, y, w, h, GUI_COLOR_TEXT);
    } else {
        unsafe {
            gfx::gfx_rect(x, y, w, h, GUI_COLOR_HIGHLIGHT);
        }
    }
}
