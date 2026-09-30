//! サーフェス (契約 G3)。
//!
//! `SurfaceId` は「描画先」の抽象。実体は (a) 全画面バックバッファの部分矩形
//! (窓のクライアント面 / フルスクリーン)、(b) オフスクリーン (主記憶、既存 `GFX_Surface`)。
//! 上限 16 (`GUI_MAX_SURFACES`)、世代付き ID (破棄後の再利用を `OS32_ERR_STALE` で弾く)。
//!
//! ポインタは surface テーブル内だけで持ち、G API には `SurfaceId` しか出さない (票 C1)。
use crate::ffi;
use crate::gstate::{refresh_screen_info, st, SurfaceEnt, SurfaceKind};
use os32api::gui::types::{Rect, SurfaceId};

/// オフスクリーンサーフェスを作る (主記憶)。上限超過/確保失敗で `SurfaceId::NULL`。
pub fn create_surface(w: i32, h: i32) -> SurfaceId {
    if w <= 0 || h <= 0 || w > 0x7FFF || h > 0x7FFF {
        return SurfaceId::NULL;
    }
    let surf = unsafe { ffi::gfx_create_surface(w, h) };
    if surf.is_null() {
        return SurfaceId::NULL;
    }
    let ent = SurfaceEnt {
        used: true,
        generation: 0,
        w: w as i16,
        h: h as i16,
        kind: SurfaceKind::Offscreen { surf },
        is_window: false,
    };
    let id = st().alloc(ent);
    if id.is_null() {
        /* テーブル満杯: 確保した GfxSurface を戻す */
        unsafe { ffi::gfx_free_surface(surf) };
    }
    id
}

/// サーフェスを破棄する。オフスクリーンの実体も解放する。
/// 戻り値: 0 成功、`OS32_ERR_STALE` 無効な ID。
pub fn destroy_surface(id: SurfaceId) -> i32 {
    let s = st();
    let idx = match s.resolve(id) {
        Some(i) => i,
        None => return os32api::gui::proto::OS32_ERR_STALE,
    };
    if let SurfaceKind::Offscreen { surf } = s.surfaces[idx].kind {
        if !surf.is_null() {
            unsafe { ffi::gfx_free_surface(surf) };
        }
    }
    if s.screen_surf.raw() == id.raw() {
        s.screen_surf = SurfaceId::NULL;
    }
    s.free(idx);
    0
}

/// 窓のクライアント面サーフェスを「矩形 + バックバッファ記述子」から作る (契約 G3)。
/// C2 が `Configure` で受けた矩形から呼ぶ。原点 `rect.x,rect.y` は全画面バックバッファの
/// 絶対座標。`is_window=true` なので Paint の基底クリップが無いと描画は拒まれる (G2)。
pub fn create_window_surface(rect: Rect) -> SurfaceId {
    if rect.w <= 0 || rect.h <= 0 {
        return SurfaceId::NULL;
    }
    let ent = SurfaceEnt {
        used: true,
        generation: 0,
        w: rect.w,
        h: rect.h,
        kind: SurfaceKind::Screen {
            ox: rect.x as i32,
            oy: rect.y as i32,
        },
        is_window: true,
    };
    st().alloc(ent)
}

/// 全画面バックバッファ全体を指す非窓サーフェス (gdi_test / デスクトップ)。
/// 一度作ってキャッシュする。`screen_info()` を信じ、640×400 を決め打ちしない (G5)。
pub fn screen_surface() -> SurfaceId {
    {
        let s = st();
        if !s.screen_surf.is_null() && s.resolve(s.screen_surf).is_some() {
            return s.screen_surf;
        }
    }
    let info = refresh_screen_info();
    let ent = SurfaceEnt {
        used: true,
        generation: 0,
        w: info.width as i16,
        h: info.height as i16,
        kind: SurfaceKind::Screen { ox: 0, oy: 0 },
        is_window: false,
    };
    let id = st().alloc(ent);
    st().screen_surf = id;
    id
}

/// サーフェスのサイズ (w,h)。無効な ID は (0,0)。
pub fn surface_size(id: SurfaceId) -> (i32, i32) {
    let s = st();
    match s.resolve(id) {
        Some(i) => (s.surfaces[i].w as i32, s.surfaces[i].h as i32),
        None => (0, 0),
    }
}

/// 内部: 描画先のオフスクリーン実体ポインタ (Offscreen のみ)。
pub(crate) fn offscreen_ptr(idx: usize) -> *mut ffi::GfxSurface {
    let s = st();
    match s.surfaces[idx].kind {
        SurfaceKind::Offscreen { surf } => surf,
        SurfaceKind::Screen { .. } => core::ptr::null_mut(),
    }
}
