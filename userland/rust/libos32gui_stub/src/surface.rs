//! surface.rs — サーフェス (契約 G3) のスタブ。表はライブラリ側 (.data)。

use crate::shcall;
use os32api::gui::stub as sh;
use os32api::gui::types::{Rect, SurfaceId};

/// オフスクリーンサーフェスを作る (主記憶)。失敗で `SurfaceId::NULL`。
pub fn create_surface(w: i32, h: i32) -> SurfaceId {
    SurfaceId(shcall!(
        sh::E_CREATE_SURFACE,
        extern "C" fn(i32, i32) -> u32,
        w,
        h
    ))
}

/// サーフェスを破棄する。0 成功、`OS32_ERR_STALE` 無効な ID。
pub fn destroy_surface(id: SurfaceId) -> i32 {
    shcall!(sh::E_DESTROY_SURFACE, extern "C" fn(u32) -> i32, id.raw())
}

/// 窓のクライアント面サーフェス (契約 G3)。
pub fn create_window_surface(rect: Rect) -> SurfaceId {
    SurfaceId(shcall!(
        sh::E_CREATE_WINDOW_SURFACE,
        extern "C" fn(Rect) -> u32,
        rect
    ))
}

/// 全画面バックバッファ全体を指す非窓サーフェス。
pub fn screen_surface() -> SurfaceId {
    SurfaceId(shcall!(sh::E_SCREEN_SURFACE, extern "C" fn() -> u32))
}

/// サーフェスのサイズ (w,h)。無効な ID は (0,0)。
pub fn surface_size(id: SurfaceId) -> (i32, i32) {
    let mut w = 0i32;
    let mut h = 0i32;
    shcall!(
        sh::E_SURFACE_SIZE,
        extern "C" fn(u32, *mut i32, *mut i32),
        id.raw(),
        &mut w as *mut i32,
        &mut h as *mut i32
    );
    (w, h)
}
