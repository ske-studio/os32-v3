//! draw.rs — 契約 G2 / G5 / G7 の描画スタブ。実体はライブラリ側 (libos32gfx 経由)。
//!
//! `Style` は 3 バイトの半端な構造体なので、境界では u32 に詰める
//! (`os32api::gui::stub::style_bits`)。

use crate::shcall;
use os32api::gui::stub::{self as sh, style_bits};
use os32api::gui::types::{Rect, ScreenInfo, Stats, Style, SurfaceId};

/// 矩形塗り (契約 G2)。`style.bg` を使う。
pub fn fill_rect(surface: SurfaceId, rect: Rect, style: Style) {
    shcall!(
        sh::E_FILL_RECT,
        extern "C" fn(u32, Rect, u32),
        surface.raw(),
        rect,
        style_bits(style)
    )
}

/// 1px 枠 (契約 G2)。`style.fg`。
pub fn draw_rect(surface: SurfaceId, rect: Rect, style: Style) {
    shcall!(
        sh::E_DRAW_RECT,
        extern "C" fn(u32, Rect, u32),
        surface.raw(),
        rect,
        style_bits(style)
    )
}

/// 水平線 (契約 G2)。
pub fn hline(surface: SurfaceId, x: i32, y: i32, w: i32, style: Style) {
    shcall!(
        sh::E_HLINE,
        extern "C" fn(u32, i32, i32, i32, u32),
        surface.raw(),
        x,
        y,
        w,
        style_bits(style)
    )
}

/// 垂直線 (契約 G2)。
pub fn vline(surface: SurfaceId, x: i32, y: i32, h: i32, style: Style) {
    shcall!(
        sh::E_VLINE,
        extern "C" fn(u32, i32, i32, i32, u32),
        surface.raw(),
        x,
        y,
        h,
        style_bits(style)
    )
}

/// 任意直線 (契約 G2)。
pub fn line(surface: SurfaceId, x0: i32, y0: i32, x1: i32, y1: i32, style: Style) {
    shcall!(
        sh::E_LINE,
        extern "C" fn(u32, i32, i32, i32, i32, u32),
        surface.raw(),
        x0,
        y0,
        x1,
        y1,
        style_bits(style)
    )
}

/// ビットマップ転送 (契約 G2、カラーキー = 色 255)。
pub fn blit(surface: SurfaceId, dx: i32, dy: i32, bitmap: SurfaceId, src_rect: Rect) {
    shcall!(
        sh::E_BLIT,
        extern "C" fn(u32, i32, i32, u32, Rect),
        surface.raw(),
        dx,
        dy,
        bitmap.raw(),
        src_rect
    )
}

/// UTF-8 文字列を `(x,y)` から描く (契約 G2)。戻り値: 送り幅 px。
pub fn text(surface: SurfaceId, x: i32, y: i32, utf8: &[u8], style: Style) -> i32 {
    shcall!(
        sh::E_TEXT,
        extern "C" fn(u32, i32, i32, *const u8, u32, u32) -> i32,
        surface.raw(),
        x,
        y,
        utf8.as_ptr(),
        utf8.len() as u32,
        style_bits(style)
    )
}

/// レイアウト用の寸法 (契約 G2)。半角 8px / 全角 16px、高さ 16px。
pub fn measure_text(utf8: &[u8]) -> (i32, i32) {
    let mut w = 0i32;
    let mut h = 0i32;
    shcall!(
        sh::E_MEASURE_TEXT,
        extern "C" fn(*const u8, u32, *mut i32, *mut i32),
        utf8.as_ptr(),
        utf8.len() as u32,
        &mut w as *mut i32,
        &mut h as *mut i32
    );
    (w, h)
}

/// 画面能力 (契約 G5)。640×400 / 16 色を決め打ちしない。
pub fn screen_info() -> ScreenInfo {
    let mut info = ScreenInfo::ZERO;
    shcall!(
        sh::E_SCREEN_INFO,
        extern "C" fn(*mut ScreenInfo),
        &mut info as *mut ScreenInfo
    );
    info
}

/// GUI カウンタ (契約 G7)。累積。
pub fn stats() -> Stats {
    let mut s = Stats::ZERO;
    shcall!(sh::E_GFX_STATS, extern "C" fn(*mut Stats), &mut s as *mut Stats);
    s
}

/// ライブラリが基底未設定の窓面描画を拒んだ累計 (テスト観測用)。
pub fn base_violation_count() -> u32 {
    shcall!(sh::E_BASE_VIOLATION_COUNT, extern "C" fn() -> u32)
}
