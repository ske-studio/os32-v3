//! icons.rs — 右ペインの 16x16 アイコン 3 種 (票 C5 §8、契約 V12-I)。
//!
//! `GuiIcon16` は 4bpp の `pixels[128]` と 1bpp の `mask[32]`。`mask = 0` の画素は
//! 描かないので、アイコンの外形はマスクで抜く (下地がそのまま見える)。データは
//! `const fn` で組んで **rodata** に置く (§8: filer の rodata でよい)。
//!
//! ```text
//!   ICON_FOLDER   ディレクトリ (タブつきフォルダ、WARN 色)
//!   ICON_EXEC     .bin (枠つきの箱 + ACCENT の山形)
//!   ICON_FILE     それ以外のファイル (角を折った紙)
//! ```

use libos32gui::gapi::proto::{
    GUI_COLOR_ACCENT, GUI_COLOR_LIGHT, GUI_COLOR_SHADOW, GUI_COLOR_TEXT, GUI_COLOR_WARN,
    GUI_COLOR_WINDOW,
};
use libos32gui::GuiIcon16;

pub static ICON_FOLDER: GuiIcon16 = build(SHAPE_FOLDER);
pub static ICON_EXEC: GuiIcon16 = build(SHAPE_EXEC);
pub static ICON_FILE: GuiIcon16 = build(SHAPE_FILE);

const SHAPE_FOLDER: u8 = 0;
const SHAPE_EXEC: u8 = 1;
const SHAPE_FILE: u8 = 2;

/* ================================================================ */
/*  形 (const fn なので if / while だけで書く)                        */
/* ================================================================ */

/// 不透明な画素か (アイコンの外形)。範囲外は透明。
const fn inside(shape: u8, x: i32, y: i32) -> bool {
    if x < 0 || y < 0 || x > 15 || y > 15 {
        return false;
    }
    match shape {
        SHAPE_FOLDER => {
            let tab = y >= 2 && y <= 4 && x >= 1 && x <= 7;
            let body = y >= 4 && y <= 13 && x >= 1 && x <= 14;
            tab || body
        }
        SHAPE_EXEC => x >= 1 && x <= 14 && y >= 2 && y <= 13,
        _ => {
            /* 紙。右上を斜めに落とす。 */
            if !(x >= 3 && x <= 12 && y >= 1 && y <= 14) {
                return false;
            }
            !(y <= 2 && x > 9 + y)
        }
    }
}

/// 外形の縁か (4 近傍のどれかが外なら縁)。
const fn is_edge(shape: u8, x: i32, y: i32) -> bool {
    !inside(shape, x - 1, y)
        || !inside(shape, x + 1, y)
        || !inside(shape, x, y - 1)
        || !inside(shape, x, y + 1)
}

const fn iabs(v: i32) -> i32 {
    if v < 0 {
        -v
    } else {
        v
    }
}

/// 画素の色 index。
const fn color_at(shape: u8, x: i32, y: i32) -> u8 {
    if is_edge(shape, x, y) {
        return GUI_COLOR_TEXT;
    }
    match shape {
        SHAPE_FOLDER => {
            /* 上端 1 行だけ明るくして立体に見せる。 */
            if y == 5 {
                GUI_COLOR_LIGHT
            } else {
                GUI_COLOR_WARN
            }
        }
        SHAPE_EXEC => {
            /* 山形 (>) を 2px 幅で。 */
            let d = 4 + iabs(y - 8);
            if y >= 4 && y <= 12 && (x == d || x == d + 1) {
                GUI_COLOR_ACCENT
            } else {
                GUI_COLOR_LIGHT
            }
        }
        _ => {
            /* 本文に見立てた横線。 */
            if (y == 5 || y == 7 || y == 9 || y == 11) && x >= 5 && x <= 10 {
                GUI_COLOR_SHADOW
            } else {
                GUI_COLOR_WINDOW
            }
        }
    }
}

/// 契約 V12-I の論理形式へ焼く (even x = 上位ニブル、mask bit7 = 左端)。
const fn build(shape: u8) -> GuiIcon16 {
    let mut px = [0u8; 128];
    let mut mask = [0u8; 32];
    let mut y = 0i32;
    while y < 16 {
        let mut x = 0i32;
        while x < 16 {
            if inside(shape, x, y) {
                let c = color_at(shape, x, y) & 0x0F;
                let i = (y * 8 + (x >> 1)) as usize;
                if x & 1 == 0 {
                    px[i] = (px[i] & 0x0F) | (c << 4);
                } else {
                    px[i] = (px[i] & 0xF0) | c;
                }
                let m = (y * 2 + (x >> 3)) as usize;
                mask[m] |= 1u8 << (7 - (x & 7));
            }
            x += 1;
        }
        y += 1;
    }
    GuiIcon16 { pixels: px, mask }
}
