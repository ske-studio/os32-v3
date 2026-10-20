//! text.rs — gshell の文字描画の 1 本 (T7b-b3 S3、設計 t7bb34 §2-2・§3-2)。
//!
//! 呼び手 15 か所の `kcg_draw_utf8` をここへ置き換えるのは S6。ここは API と
//! 描画の中身だけ:
//!
//! - [`draw`] (不透過: cell∩clip を `bg` で塗ってから `fg`) / [`draw_t`] (透過)
//!   / [`draw_x2`] (32px: 16px の bitmap を最近傍 2 倍、Engine には Px16 で引く)。
//! - 幅と制御字は `kcg_draw_utf8` (gfx_kcg.c:87-100) と同じ: `unicode_to_ank` が
//!   0 でなければ 8px、0 なら 16px。cp<0x20 と U+FEFF は送らない、`\t` は +32、
//!   `\n` は行頭へ戻り 18px 下げる。`kcg_set_scale` は呼ばない。
//! - 経路: ANK byte が 0x20..=0x7E で **0x5C 以外**は TTF (cp = その ASCII、
//!   U+FF21 'Ａ' も 'A' と同じ entry)、0x5C (¥) と半角カナは ANK (ROM)、全角は TTF。
//!   cache に無い字・欠字・slot 超過・engine が KCG のときは KCG
//!   (`kcg_read_ank` / `unicode_to_jis` → `kcg_read_kanji`、無ければ 0x2222)。
//! - 字は `cell.y + ascent - top` から。**cell 矩形 (8|16 × 16) の外の画素は描かない**、
//!   その上で `clip` で画素単位に切る (行の範囲は pat の先頭と `h_lines`、列は
//!   bit mask)。描画口は libos32gfx の `gfx_draw_font` だけ [HW1]。

use crate::chrome::utf8_next;
use crate::ffi;
use crate::font;
use crate::wm::Rect;
use os32_font::cache::Entry;
use os32_font::engine::{Cell, Glyph};
use os32_font::limits::CELL_H_PX;

/// cell の高さ (16px、font crate の正典)。
pub const CELL_H: i32 = CELL_H_PX as i32;
/// 半角 / 全角の cell 幅。
pub const ANK_W: i32 = 8;
pub const KANJI_W: i32 = 16;
/// `\n` の送り (kcg_draw_utf8 の line_h)。
pub const LINE_H: i32 = 18;
/// `\t` の送り (ANK 4 桁)。
pub const TAB_W: i32 = ANK_W * 4;
/// ROM の ¥ (ANK 0x5C)。Inconsolata の `\` は描かない。
const ANK_YEN: u8 = 0x5C;
/// JIS に無い字の代わり (□、gfx_kcg.c と同じ)。
const JIS_FALLBACK: u16 = 0x2222;

/// 1 字の行き先。
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Route {
    /// 送らない (cp<0x20 のうち \t \n 以外、U+FEFF)。
    Skip,
    Tab,
    Newline,
    /// ROM の ANK (¥・半角カナ)。
    Ank(u8),
    /// TTF (cache)。`ank` は KCG に落ちたときの ANK byte (全角は 0)。
    Ttf { cp: u32, cell: Cell, ank: u8 },
}

/// 1 字の経路と幅の規則 (設計 §2-2 の 2)。
pub fn route(cp: u32) -> Route {
    if cp == u32::from(b'\n') {
        return Route::Newline;
    }
    if cp == u32::from(b'\t') {
        return Route::Tab;
    }
    if cp < 0x20 || cp == 0xFEFF {
        return Route::Skip;
    }
    let ank = unsafe { ffi::unicode_to_ank(cp) };
    if ank == 0 {
        return Route::Ttf { cp, cell: Cell::Full, ank: 0 };
    }
    if (0x20..=0x7E).contains(&ank) && ank != ANK_YEN {
        return Route::Ttf { cp: u32::from(ank), cell: Cell::Half, ank };
    }
    Route::Ank(ank)
}

/// 文字列の TTF 経路の字 (raster の対象と need_raster の照会)。
pub fn ttf_glyphs(s: &[u8]) -> TtfGlyphs<'_> {
    TtfGlyphs { s, i: 0 }
}

pub struct TtfGlyphs<'a> {
    s: &'a [u8],
    i: usize,
}

impl Iterator for TtfGlyphs<'_> {
    type Item = (u32, Cell);
    fn next(&mut self) -> Option<(u32, Cell)> {
        while self.i < self.s.len() && self.s[self.i] != 0 {
            let (n, cp) = utf8_next(self.s, self.i);
            self.i += n;
            if let Route::Ttf { cp, cell, .. } = route(cp) {
                return Some((cp, cell));
            }
        }
        None
    }
}

/// 不透過で描く。戻り値は送った幅 (最後の行、`kcg_draw_utf8` と同じ)。
#[allow(dead_code)] // 呼び手は S6
pub fn draw(clip: Rect, x: i32, y: i32, s: &[u8], fg: u8, bg: u8) -> i32 {
    draw_ex(clip, x, y, s, fg, Some(bg), 1)
}

/// 透過で描く (`fg` の画素だけ)。
#[allow(dead_code)] // 呼び手は S6
pub fn draw_t(clip: Rect, x: i32, y: i32, s: &[u8], fg: u8) -> i32 {
    draw_ex(clip, x, y, s, fg, None, 1)
}

/// 32px (16px の bitmap を最近傍 2 倍)。`bg` が `None` なら透過。
#[allow(dead_code)] // 呼び手は S6
pub fn draw_x2(clip: Rect, x: i32, y: i32, s: &[u8], fg: u8, bg: Option<u8>) -> i32 {
    draw_ex(clip, x, y, s, fg, bg, 2)
}

/// 1 cell の 1bpp (行ごと u16、MSB = 左端の画素、半角は上位 8 bit だけ)。
type CellRows = [u16; CELL_H_PX];

fn draw_ex(clip: Rect, x: i32, y: i32, s: &[u8], fg: u8, bg: Option<u8>, scale: i32) -> i32 {
    let usable = font::begin_text(s);
    let mut cx = x;
    let mut cy = y;
    let mut i = 0;
    while i < s.len() && s[i] != 0 {
        let (n, cp) = utf8_next(s, i);
        i += n;
        match route(cp) {
            Route::Skip => {}
            Route::Newline => {
                cx = x;
                cy += LINE_H * scale;
            }
            Route::Tab => cx += TAB_W * scale,
            Route::Ank(ank) => {
                let rows = kcg_ank(ank);
                put_cell(clip, cx, cy, ANK_W, scale, &rows, fg, bg);
                cx += ANK_W * scale;
            }
            Route::Ttf { cp, cell, ank } => {
                let w = if cell == Cell::Half { ANK_W } else { KANJI_W };
                let mut rows: CellRows = [0; CELL_H_PX];
                let drawn = if usable {
                    font::with_glyph(cp, |g, bits| rows = cell_from_glyph(g, bits, w))
                } else {
                    None
                };
                if drawn.is_none() {
                    rows = if ank != 0 { kcg_ank(ank) } else { kcg_kanji(cp) };
                }
                put_cell(clip, cx, cy, w, scale, &rows, fg, bg);
                cx += w * scale;
            }
        }
    }
    font::end_text();
    cx - x
}

/// ROM の ANK 8×16 (cell の先頭行から)。
fn kcg_ank(ank: u8) -> CellRows {
    let mut pat = [0u8; CELL_H_PX];
    unsafe { (os32api::api().kcg_read_ank)(ank, pat.as_mut_ptr()) };
    let mut rows: CellRows = [0; CELL_H_PX];
    for (r, b) in pat.iter().enumerate() {
        rows[r] = u16::from(*b) << 8;
    }
    rows
}

/// ROM の漢字 16×16。JIS に無い字は □ (0x2222)。
fn kcg_kanji(cp: u32) -> CellRows {
    let mut jis = unsafe { (os32api::api().unicode_to_jis)(cp) };
    if jis == 0 {
        jis = JIS_FALLBACK;
    }
    let mut pat = [0u8; CELL_H_PX * 2];
    unsafe { (os32api::api().kcg_read_kanji)(jis, pat.as_mut_ptr()) };
    let mut rows: CellRows = [0; CELL_H_PX];
    let mut r = 0;
    while r < CELL_H_PX {
        rows[r] = (u16::from(pat[2 * r]) << 8) | u16::from(pat[2 * r + 1]);
        r += 1;
    }
    rows
}

/// cache の bitmap を cell (幅 `w`、16 行) に置く: 左上は (`left`, `ascent - top`)、
/// cell の外へ出る行・列は捨てる (G2 の cell 契約)。
pub fn cell_from_glyph(g: &Glyph, bits: &[u8], w: i32) -> CellRows {
    let mut rows: CellRows = [0; CELL_H_PX];
    let e: &Entry = &g.entry;
    let pitch = usize::from(e.pitch);
    if pitch == 0 || pitch > 4 {
        return rows;
    }
    let oy = i32::from(g.ascent) - i32::from(e.top);
    let ox = i32::from(e.left);
    /* cell 幅の外の列を落とす mask (半角は上位 8 bit)。 */
    let wmask: u16 = if w >= 16 { 0xFFFF } else { 0xFF00 };
    let mut r = 0usize;
    while r < usize::from(e.height) {
        let dy = oy + r as i32;
        if dy >= 0 && dy < CELL_H {
            let base = r * pitch;
            let mut v: u32 = 0;
            let mut k = 0;
            while k < pitch {
                let b = bits.get(base + k).copied().unwrap_or(0);
                v |= u32::from(b) << (24 - 8 * k as u32);
                k += 1;
            }
            let shifted = if ox >= 32 || ox <= -32 {
                0
            } else if ox >= 0 {
                v >> ox
            } else {
                v << (-ox)
            };
            rows[dy as usize] = ((shifted >> 16) as u16) & wmask;
        }
        r += 1;
    }
    rows
}

/// 16 bit を 32 bit へ横 2 倍 (MSB 側から)。
fn widen2(v: u16) -> u32 {
    let mut out = 0u32;
    let mut b = 0;
    while b < 16 {
        if v & (0x8000 >> b) != 0 {
            out |= 0xC000_0000 >> (2 * b);
        }
        b += 1;
    }
    out
}

/// 1 cell を clip で切って置く。`bg` があれば cell∩clip を先に塗る。
#[allow(clippy::too_many_arguments)]
fn put_cell(clip: Rect, cx: i32, cy: i32, w: i32, scale: i32, rows: &CellRows, fg: u8, bg: Option<u8>) {
    let cw = w * scale;
    let ch = CELL_H * scale;
    let cell = Rect::new(cx, cy, cw, ch);
    let r = cell.intersect(&clip);
    if r.is_empty() {
        return;
    }
    if let Some(bg) = bg {
        unsafe { os32api::gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, bg) };
    }
    /* 列の mask (cell 座標 [c0, c1))。 */
    let c0 = r.x - cx;
    let c1 = r.right() - cx;
    let mut cmask: u32 = 0;
    let mut c = c0;
    while c < c1 {
        cmask |= 0x8000_0000 >> c;
        c += 1;
    }
    let wb = (cw / 8) as usize; /* 1 / 2 / 4 バイト */
    let row0 = r.y - cy;
    let row1 = r.bottom() - cy;
    let mut pat = [0u8; 4 * CELL_H_PX * 2];
    let mut any = false;
    let mut row = row0;
    while row < row1 {
        let src = rows[(row / scale) as usize];
        let v = (if scale == 2 { widen2(src) } else { u32::from(src) << 16 }) & cmask;
        if v != 0 {
            any = true;
        }
        let o = (row - row0) as usize * wb;
        let mut k = 0;
        while k < wb {
            pat[o + k] = (v >> (24 - 8 * k as u32)) as u8;
            k += 1;
        }
        row += 1;
    }
    if !any {
        return;
    }
    unsafe { ffi::gfx_draw_font(cx, cy + row0, pat.as_ptr(), wb as i32, row1 - row0, fg) };
}

#[cfg(test)]
#[path = "../host/text_tests.rs"]
mod tests;
