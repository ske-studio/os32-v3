//! desktop.rs — デスクトップ (背景)。v1.2 でタスクバー。
//!
//! `wm::composite_rect` から「どのウィンドウにも覆われていない矩形」ごとに
//! 呼ばれる。塗るのは背景色だけ — 色は設定レジストリの
//! `gshell` / `desktop/color` (票 S4 §1、既定は G6 の `DESKTOP` = 12) で、
//! 起動時に 1 回読んだ適用値 (`GuiState.cfg.desktop_color`) を見る。
//!
//! 上部の手引きバー (`OS32 GUI shell ESC:CUI F1..F5`) は **G5 で製品から撤去**
//! した — `crate::DEBUG_SHORTCUTS` が `false` の間 [`hint_rect`] は空矩形を返し、
//! [`draw_hint`] は何も描かないので、そこは素の背景になる。バーは作業領域
//! (`wm::work_area` = 画面 − タスクバー) を取っていないので、撤去で空く座標は無い。
//! デバッグで `true` に戻したときだけ、文字は**掛かるウィンドウが無いときに
//! だけ**描く (kcg にはクリップが無く、窓の上へはみ出せないため)。
//!
//! 14 色リース中 (契約 G8) はシステム色が使えないので、**不可侵の 2 色
//! (`TEXT` / `WINDOW`) の市松**で塗る。1 画素単位の市松は面積が広すぎて
//! 386 では割に合わないので、8x8 の粗い市松にしてある (Windows 1.x の
//! デスクトップと同じ骨格)。

use crate::lease;
use crate::wm::{GuiState, Rect};
use os32api::gfx;
use os32api::gui::proto::{GUI_COLOR_TEXT, GUI_COLOR_TITLE_TEXT, GUI_COLOR_WINDOW};

/// 手引きの位置 (左上) と占有矩形。ANK 8x16 なので 1 文字 8px。
const HINT_X: i32 = 8;
const HINT_Y: i32 = 8;
const HINT_W: i32 = 8 * 65;
const HINT_H: i32 = 16;

/// 市松の升目 (画素)。
const CHECK: i32 = 8;

/// 単独起動時の手引き (UTF-8, NUL 終端)。デバッグ専用 (`DEBUG_SHORTCUTS`)。
static HINT: &[u8] =
    b"OS32 GUI shell  ESC:CUI F1:demo F2:bench F3:busy F4:lease F5:open\0";

/// 背景の 1 矩形を塗る (画面座標、バックバッファ)。
pub fn fill(st: &GuiState, r: Rect) {
    if r.is_empty() {
        return;
    }
    if !lease::mono(st) {
        /* 背景色は設定レジストリの `gshell/desktop/color` (票 S4 §1)。
         * 既定は `GUI_COLOR_DESKTOP` (12) なので、DB が無くても見た目は変わらない。 */
        unsafe {
            gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, st.cfg.desktop_color);
        }
        return;
    }
    /* 2 色モード: 白地に黒の 8x8 市松。画面格子に合わせるので、矩形を
     * またいで描き直しても継ぎ目が出ない。 */
    unsafe {
        gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, GUI_COLOR_WINDOW);
    }
    let x0 = (r.x / CHECK) * CHECK;
    let y0 = (r.y / CHECK) * CHECK;
    let mut y = y0;
    while y < r.bottom() {
        let mut x = x0;
        while x < r.right() {
            if (((x / CHECK) + (y / CHECK)) & 1) == 0 {
                let cell = Rect::new(x, y, CHECK, CHECK).intersect(&r);
                if !cell.is_empty() {
                    unsafe {
                        gfx::gfx_fill_rect(cell.x, cell.y, cell.w, cell.h, GUI_COLOR_TEXT);
                    }
                }
            }
            x += CHECK;
        }
        y += CHECK;
    }
}

/// 手引きの占有矩形。製品 (`DEBUG_SHORTCUTS == false`) では空 (契約 S6 / W3 §4.1)。
/// 空矩形は `intersects` が常に偽なので、`composite_rect` は `draw_hint` を呼ばない。
#[inline]
pub fn hint_rect() -> Rect {
    if !crate::DEBUG_SHORTCUTS {
        return Rect::EMPTY;
    }
    Rect::new(HINT_X, HINT_Y, HINT_W, HINT_H)
}

/// 手引きの文字を描く (デバッグ専用)。掛かるウィンドウが 1 枚でもあれば描かない
/// (kcg_draw_utf8 にクリップが無いので、はみ出して窓を壊さないための保険)。
pub fn draw_hint(st: &GuiState) {
    if !crate::DEBUG_SHORTCUTS {
        return;
    }
    let hr = hint_rect();
    let mut z = 0;
    while z < st.z_count {
        let idx = st.zorder[z];
        let w = &st.windows[idx];
        if w.used && w.visible && w.outer().intersects(&hr) {
            return;
        }
        z += 1;
    }
    let (fg, bg) = if lease::mono(st) {
        (GUI_COLOR_WINDOW, GUI_COLOR_TEXT)
    } else {
        (GUI_COLOR_TITLE_TEXT, st.cfg.desktop_color)
    };
    unsafe {
        gfx::kcg_set_scale(1);
        gfx::kcg_draw_utf8(HINT_X, HINT_Y, HINT.as_ptr(), fg, bg);
    }
}
