//! icon.rs — 16x16 標準アイコン (契約 V12-I) のスタブ。描画実体はライブラリ側。
//!
//! ```text
//!   pixels[128]  16*16*4bpp row-major。even x = 上位ニブル / odd x = 下位ニブル
//!   mask[32]     16*16*1bpp row-major。bit7 が左端。1 = 不透明 / 0 = 透明
//! ```
//!
//! 色 index は GUI system 16 色 (`proto::GUI_COLOR_*`)。`mask = 0` は描かない。
//! clipping はライブラリ側が守る。拡大縮小しない。9801 planar / PEGC PACKED8 /
//! Cirrus の違いは呼び出し側に見えない。

use crate::shcall;
use os32api::gui::stub as sh;
use os32api::gui::types::SurfaceId;

pub use os32api::gui::stub::GuiIcon16;

/// アイコンの一辺 (px)。v1.2 は 16 固定。
pub const ICON16_SIZE: i32 = 16;

/// アイコンを `(x, y)` (サーフェスローカル座標) の左上へ描く (契約 V12-I)。
pub fn draw_icon16(surface: SurfaceId, x: i32, y: i32, icon: &GuiIcon16) {
    shcall!(
        sh::E_DRAW_ICON16,
        extern "C" fn(u32, i32, i32, *const GuiIcon16),
        surface.raw(),
        x,
        y,
        icon as *const GuiIcon16
    )
}
