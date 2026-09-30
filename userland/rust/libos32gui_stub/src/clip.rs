//! clip.rs — クリップ (契約 G2) のスタブ。スタックはライブラリ側 (.data)。

use crate::shcall;
use os32api::gui::stub as sh;
use os32api::gui::types::{Rect, SurfaceId};

/// 処理中の `Paint` 矩形をこのサーフェスの基底クリップに固定する (契約 G2)。
pub fn set_base_clip(surface: SurfaceId, rect: Rect) -> i32 {
    shcall!(
        sh::E_SET_BASE_CLIP,
        extern "C" fn(u32, Rect) -> i32,
        surface.raw(),
        rect
    )
}

/// 基底クリップを解除する (Paint の処理が終わったとき)。
pub fn clear_base_clip() {
    shcall!(sh::E_CLEAR_BASE_CLIP, extern "C" fn())
}

/// 現在のクリップの内側にさらに矩形を掛ける。戻り値: 新しい深さ、または負値。
pub fn push_clip(rect: Rect) -> i32 {
    shcall!(sh::E_PUSH_CLIP, extern "C" fn(Rect) -> i32, rect)
}

/// [`push_clip`] を 1 段戻す。基底 (深さ 0) は外せない。
pub fn pop_clip() {
    shcall!(sh::E_POP_CLIP, extern "C" fn())
}

/// 現在の実効クリップ矩形 (サーフェスローカル座標)。
pub fn current_clip() -> Rect {
    let mut r = Rect::EMPTY;
    shcall!(
        sh::E_CURRENT_CLIP,
        extern "C" fn(*mut Rect),
        &mut r as *mut Rect
    );
    r
}
