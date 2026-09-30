//! icon.rs — 16x16 標準アイコン (票 C4、契約 V12-I)。
//!
//! 論理形式は [`GuiIcon16`] (160B) 1 種類だけ。WM (gshell) と libos32gui は
//! **同じ形**を使う。PNG / BMP / ICO の解読は v1.4 以降 (契約 §8)。
//!
//! ```text
//!   pixels[128]  16*16*4bpp row-major。even x = 上位ニブル / odd x = 下位ニブル
//!   mask[32]     16*16*1bpp row-major。bit7 が左端。1 = 不透明 / 0 = 透明
//! ```
//!
//! - 色 index は GUI system 16 色 (`proto::GUI_COLOR_*`)。
//! - `mask = 0` の画素は**書かない** (下地がそのまま見える)。
//! - clipping は必ず守る (基底クリップの外へ 1 画素も出さない)。
//! - 拡大縮小しない。
//! - 9801 planar / PEGC PACKED8 / Cirrus の違いは [`draw::Painter`] が吸収するので
//!   呼び出し側からは見えない (契約 V12-I)。

use crate::clip;
use crate::draw::Painter;
use os32api::gui::types::SurfaceId;

pub use os32api::gui::stub::GuiIcon16;

/// アイコンの一辺 (px)。v1.2 は 16 固定。
pub const ICON16_SIZE: i32 = 16;

/// アイコンを `(x, y)` (サーフェスローカル座標) の左上へ描く (契約 V12-I)。
///
/// `mask` が 0 の画素は書かない。クリップ (基底 + push_clip) の外へも書かない。
/// 拡大縮小はしない。窓のクライアント面に描けるのは `Paint` 処理中だけ (契約 G2)。
pub fn draw_icon16(surface: SurfaceId, x: i32, y: i32, icon: &GuiIcon16) {
    let t = match clip::resolve_target(surface) {
        Some(t) => t,
        None => return,
    };
    if t.clip.is_empty() {
        return;
    }
    let p = Painter::from_target(&t);

    /* アイコンごとクリップの外なら何もしない。 */
    if p.cell_hidden(x, y, ICON16_SIZE, ICON16_SIZE) {
        return;
    }
    let mut row = 0i32;
    while row < ICON16_SIZE {
        let mrow = (row * 2) as usize;
        let prow = (row * 8) as usize;
        /* 行のクリップ判定は 1 回だけ (PACKED8 の画面のみ。他は put へ)。 */
        let fast = p.row(y + row);
        let mut col = 0i32;
        while col < ICON16_SIZE {
            let mbits = icon.mask[mrow + (col >> 3) as usize];
            if (mbits >> (7 - (col & 7))) & 1 != 0 {
                let pb = icon.pixels[prow + (col >> 1) as usize];
                let c = if col & 1 == 0 { pb >> 4 } else { pb & 0x0F };
                match &fast {
                    Some(r) => r.put(x + col, c),
                    /* put がクリップ (と PACKED8 の物理境界) を見る。 */
                    None => p.put(x + col, y + row, c),
                }
            }
            col += 1;
        }
        row += 1;
    }
}
