//! libos32gfx (C) への FFI 宣言。
//!
//! **鉄則 (v2 C8)**: 描画の実体は libos32gfx。ここではその C 関数を宣言するだけで、
//! Rust 側でプレーンへ直接ピクセルを書かない。`libos32gfx.{c,h}` (C 側) は触らず、
//! FFI 宣言はすべてこの Rust 側に置く (票 C1 の所有権どおり)。
//!
//! 座標系: `gfx_*` は全画面バックバッファ (`gfx_fb`) の絶対座標に書く。サーフェス
//! ローカル座標 → 絶対座標の変換とクリップは `draw` / `surface` 側で行う。
#![allow(dead_code)]
#![allow(non_camel_case_types)]

/// C `GFX_Rect` (os32_kapi_shared.h) と同一レイアウト。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GfxRect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

/// C `GFX_Framebuffer` (os32_kapi_shared.h)。planes[0..3] = B/R/G/I (PLANAR4)。
/// PACKED8 バックエンドでは planes[0] が線形バッファ、pitch はバイト/ライン。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GfxFramebuffer {
    pub width: i32,
    pub height: i32,
    pub pitch: i32,
    pub planes: [*mut u8; 4],
}

/// C `GFX_Surface` (os32_kapi_shared.h)。オフスクリーン (主記憶) の描画バッファ。
/// ポインタは Rust 内 (surface テーブル) だけで持ち、G API には SurfaceId しか出さない。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GfxSurface {
    pub w: i32,
    pub h: i32,
    pub pitch: i32,
    pub planes: [*mut u8; 4],
    pub _pool_idx: i32,
}

extern "C" {
    /// 全画面バックバッファ記述子 (libos32gfx の可変グローバル)。読むだけ。
    pub static gfx_fb: GfxFramebuffer;

    /* --- 全画面バックバッファへの描画 (絶対座標) --- */
    pub fn gfx_pixel(x: i32, y: i32, color: u8);
    pub fn gfx_get_pixel(x: i32, y: i32) -> u8; /* バックバッファ読み戻し (VRAM ではない) */
    pub fn gfx_hline(x: i32, y: i32, w: i32, color: u8);
    pub fn gfx_vline(x: i32, y: i32, h: i32, color: u8);
    pub fn gfx_line(x0: i32, y0: i32, x1: i32, y1: i32, color: u8);
    pub fn gfx_fill_rect(x: i32, y: i32, w: i32, h: i32, color: u8);

    /* --- KCG 文字 (全画面バックバッファ、絶対座標) --- */
    pub fn kcg_set_scale(scale: i32);
    pub fn kcg_draw_ank(x: i32, y: i32, ch: u8, fg: u8, bg: u8);
    pub fn kcg_draw_kanji(x: i32, y: i32, jis_code: u16, fg: u8, bg: u8);
    pub fn kcg_draw_utf8(x: i32, y: i32, utf8_str: *const u8, fg: u8, bg: u8) -> i32;

    /* --- オフスクリーンサーフェス --- */
    pub fn gfx_create_surface(w: i32, h: i32) -> *mut GfxSurface;
    pub fn gfx_free_surface(surf: *mut GfxSurface);
    pub fn gfx_surface_clear(surf: *mut GfxSurface, color: u8);
    pub fn gfx_surface_pixel(surf: *mut GfxSurface, x: i32, y: i32, color: u8);
    pub fn gfx_surface_fill_rect(surf: *mut GfxSurface, x: i32, y: i32, w: i32, h: i32, color: u8);

    /* --- ビットマップ転送 (dst = 全画面バックバッファ、絶対座標) --- */
    pub fn gfx_blit(dx: i32, dy: i32, src: *const GfxSurface, src_rect: *const GfxRect);
    pub fn gfx_blit_colorkey(
        dx: i32,
        dy: i32,
        src: *const GfxSurface,
        src_rect: *const GfxRect,
        colorkey: u8,
    );
}
