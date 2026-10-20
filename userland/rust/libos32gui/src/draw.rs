//! G 描画プリミティブ (契約 G2) — libos32gfx の上に載せる。
//!
//! **鉄則 (票 C1 / v2 C8)**:
//! - 描画の実体は libos32gfx を FFI で呼ぶ。プレーンへ直接ピクセルを書かない
//!   (PLANAR4)。線・枠はクライアント側の CPU 実装 (幾何) だが、点は `gfx_pixel`
//!   (libos32gfx) を通す。8bpp (`GFX_FMT_PACKED8`) だけは契約どおり C1 の CPU 実装で
//!   線形バッファへ直接書く分岐を用意する (v1.1 前半は PLANAR4 のみ動けばよい)。
//! - **クリップは自分で守る**: サーフェス外へは 1 ピクセルも出さない。矩形は交差で、
//!   線は 1 ピクセルごとにクリップ判定で、文字は画素単位 (さらに cell の外は描かない) で守る。
//! - ステートレス: 呼び出しごとに `Style` を受ける。DC 相当の選択状態は持たない。
use crate::clip::{self, Target};
use crate::ffi;
use crate::textcore;
use crate::gstate::{screen_info_cached, st, SurfaceKind};
use crate::surface::offscreen_ptr;
use os32api::gui::proto::{
    GUI_STYLE_DITHER50, GUI_STYLE_DOTTED, GUI_STYLE_TRANSPARENT_BG, GUI_STYLE_XOR,
};
use os32api::gui::types::{Rect, ScreenInfo, Stats, Style, SurfaceId, GFX_FMT_PACKED8};
use crate::font::AppEngine;
use os32_font::engine::{Cell as FontCell, Glyph, Resolved};
use os32_font::raster::PixelSize;

const CELL_H: i32 = os32_font::limits::CELL_H_PX as i32; /* セル高 (scale1、font crate の正典) */
const ANK_W: i32 = 8; /* 半角セル幅 */
const KANJI_W: i32 = 16; /* 全角セル幅 */

/* ================================================================ */
/*  低レベル画素バックエンド (Target に対して局所座標で書く)         */
/* ================================================================ */

/// 画素バックエンド。`icon.rs` も使うので crate 内公開 (票 C4)。
pub(crate) struct Painter {
    ox: i32,
    oy: i32,
    offscreen: *mut ffi::GfxSurface, /* null = 画面 */
    packed8: bool,
    fb_base: *mut u8,
    fb_pitch: i32,
    fb_w: i32,  /* 物理画面幅 (PACKED8 直書きの境界。レビュー ③) */
    fb_h: i32,  /* 物理画面高 */
    clip: Rect, /* 局所座標、サーフェス境界内 */
}

impl Painter {
    pub(crate) fn from_target(t: &Target) -> Painter {
        if !t.offscreen && unsafe { core::ptr::read_volatile(core::ptr::addr_of!(ffi::gfx_ready)) } == 0 {
            return Painter {
                ox: 0, oy: 0, offscreen: core::ptr::null_mut(), packed8: true,
                fb_base: core::ptr::null_mut(), fb_pitch: 0, fb_w: 0, fb_h: 0,
                clip: Rect::EMPTY,
            };
        }
        let info = screen_info_cached();
        let packed8 = !t.offscreen && info.format == GFX_FMT_PACKED8;
        let (fb_base, fb_pitch) = if packed8 {
            let fb = unsafe { core::ptr::read_volatile(core::ptr::addr_of!(ffi::gfx_fb)) };
            (fb.planes[0], fb.pitch)
        } else {
            (core::ptr::null_mut(), 0)
        };
        Painter {
            ox: t.ox,
            oy: t.oy,
            offscreen: if t.offscreen { offscreen_ptr(t.idx) } else { core::ptr::null_mut() },
            packed8,
            fb_base,
            fb_pitch,
            fb_w: info.width as i32,
            fb_h: info.height as i32,
            clip: t.clip,
        }
    }

    #[inline]
    fn in_clip(&self, lx: i32, ly: i32) -> bool {
        self.clip.contains(lx, ly)
    }

    /// 1 画素 (局所座標、クリップ判定つき)。
    #[inline]
    pub(crate) fn put(&self, lx: i32, ly: i32, color: u8) {
        if !self.in_clip(lx, ly) {
            return;
        }
        if !self.offscreen.is_null() {
            unsafe { ffi::gfx_surface_pixel(self.offscreen, lx, ly, color) };
        } else if self.packed8 {
            /* PACKED8 は Rust が直接 framebuffer を書くので、局所クリップに加えて
             * 物理画面境界も最終チェックする (画面外 Window Surface での OOB write 防止、
             * レビュー ③)。 */
            let gx = self.ox + lx;
            let gy = self.oy + ly;
            if !self.fb_base.is_null()
                && gx >= 0 && gy >= 0 && gx < self.fb_w && gy < self.fb_h
            {
                let off = gy as isize * self.fb_pitch as isize + gx as isize;
                unsafe { *self.fb_base.offset(off) = color };
            }
        } else {
            unsafe { ffi::gfx_pixel(self.ox + lx, self.oy + ly, color) };
        }
    }

    /// 1 画素の読み戻し (バックバッファ。VRAM は読まない)。画面のみ。
    #[inline]
    fn get(&self, lx: i32, ly: i32) -> u8 {
        if !self.offscreen.is_null() {
            0 /* オフスクリーンの読み戻しは libos32gfx に口が無い (XOR は画面用) */
        } else if self.packed8 {
            let gx = self.ox + lx;
            let gy = self.oy + ly;
            if self.fb_base.is_null()
                || gx < 0 || gy < 0 || gx >= self.fb_w || gy >= self.fb_h
            {
                0
            } else {
                let off = gy as isize * self.fb_pitch as isize + gx as isize;
                unsafe { *self.fb_base.offset(off) }
            }
        } else {
            unsafe { ffi::gfx_get_pixel(self.ox + lx, self.oy + ly) }
        }
    }

    /// 矩形塗り。**クリップと物理画面境界の交差は 1 回だけ**取り、内側は
    /// 1 行 `write_bytes` (memset) で埋める。
    ///
    /// 以前はここが 1 画素ごとに `put` を呼んでいて、画素あたり 8 本前後の
    /// 比較 (クリップ 4 + 画面境界 4) を回していた。filer のペイン再描画は
    /// 10 万画素規模なので、実測で 1 打鍵あたり約 1.36 秒かかっていた
    /// (v1.2 G3: EIP サンプル 11/11 がこのループの中)。判定を外へ出すだけで
    /// 内側は「1 行 = memset 1 回」になる。
    fn fill_solid(&self, rc: Rect, color: u8) {
        let rc = rc.intersect(&self.clip);
        if rc.is_empty() {
            return;
        }
        if !self.offscreen.is_null() {
            unsafe {
                ffi::gfx_surface_fill_rect(
                    self.offscreen,
                    rc.x as i32,
                    rc.y as i32,
                    rc.w as i32,
                    rc.h as i32,
                    color,
                )
            };
        } else if self.packed8 {
            if self.fb_base.is_null() {
                return;
            }
            /* 画面外 Window Surface での OOB write 防止 (レビュー ③) は
             * ここで 1 回だけ効かせる。 */
            let mut gx0 = self.ox + rc.x as i32;
            let mut gy0 = self.oy + rc.y as i32;
            let mut gx1 = self.ox + rc.right();
            let mut gy1 = self.oy + rc.bottom();
            if gx0 < 0 {
                gx0 = 0;
            }
            if gy0 < 0 {
                gy0 = 0;
            }
            if gx1 > self.fb_w {
                gx1 = self.fb_w;
            }
            if gy1 > self.fb_h {
                gy1 = self.fb_h;
            }
            if gx1 <= gx0 || gy1 <= gy0 {
                return;
            }
            let n = (gx1 - gx0) as usize;
            let mut gy = gy0;
            while gy < gy1 {
                let off = gy as isize * self.fb_pitch as isize + gx0 as isize;
                unsafe { core::ptr::write_bytes(self.fb_base.offset(off), color, n) };
                gy += 1;
            }
        } else {
            unsafe {
                ffi::gfx_fill_rect(
                    self.ox + rc.x as i32,
                    self.oy + rc.y as i32,
                    rc.w as i32,
                    rc.h as i32,
                    color,
                )
            };
        }
    }

    /// この矩形はクリップに全く掛からないか (セル単位の早期打ち切り用)。
    #[inline]
    pub(crate) fn cell_hidden(&self, lx: i32, ly: i32, w: i32, h: i32) -> bool {
        Rect::new(lx as i16, ly as i16, w as i16, h as i16)
            .intersect(&self.clip)
            .is_empty()
    }

    /// PACKED8 の画面サーフェスなら、その行への直書き情報を返す。
    /// クリップと画面境界を**行ごとに 1 回**畳んでおき、1 画素の書き込みは
    /// 範囲比較 1 つ + ストア 1 つになる。オフスクリーン / PLANAR4 は `None`
    /// (呼び出し側は従来どおり `put` を使う)。
    #[inline]
    pub(crate) fn row(&self, ly: i32) -> Option<Row> {
        if !self.offscreen.is_null() || !self.packed8 || self.fb_base.is_null() {
            return None;
        }
        if ly < self.clip.y as i32 || ly >= self.clip.bottom() {
            return None;
        }
        let gy = self.oy + ly;
        if gy < 0 || gy >= self.fb_h {
            return None;
        }
        let mut lo = self.clip.x as i32;
        let mut hi = self.clip.right();
        if lo < -self.ox {
            lo = -self.ox;
        }
        if hi > self.fb_w - self.ox {
            hi = self.fb_w - self.ox;
        }
        if hi <= lo {
            return None;
        }
        let base = unsafe { self.fb_base.offset(gy as isize * self.fb_pitch as isize) };
        Some(Row { base, ox: self.ox, lo, hi })
    }
}

/// `Painter::row` が返す 1 行の直書き口 (PACKED8 の画面のみ)。
pub(crate) struct Row {
    base: *mut u8, /* その行の物理 x=0 */
    ox: i32,
    lo: i32, /* 書いてよい局所 x の下限 */
    hi: i32, /* 同上限 (排他) */
}

impl Row {
    #[inline]
    pub(crate) fn put(&self, lx: i32, color: u8) {
        if lx >= self.lo && lx < self.hi {
            unsafe { *self.base.offset((self.ox + lx) as isize) = color };
        }
    }
}

/* ================================================================ */
/*  クリップ済み矩形ヘルパ                                           */
/* ================================================================ */

#[inline]
fn clip_rect(t: &Target, r: Rect) -> Rect {
    r.intersect(&t.clip)
}

/* ================================================================ */
/*  塗り / 枠                                                        */
/* ================================================================ */

/// 矩形塗り (契約 G2)。`style.bg` を使う。
/// フラグ: `TRANSPARENT_BG` = 何もしない / `DITHER50` = 市松 (fg,bg) / `XOR` = 画素反転。
pub fn fill_rect(surface: SurfaceId, rect: Rect, style: Style) {
    let t = match clip::resolve_target(surface) {
        Some(t) => t,
        None => return,
    };
    let rc = clip_rect(&t, rect);
    if rc.is_empty() {
        return;
    }
    let p = Painter::from_target(&t);

    if style.has(GUI_STYLE_TRANSPARENT_BG) {
        return;
    }
    if style.has(GUI_STYLE_XOR) {
        let mut ly = rc.y as i32;
        let y1 = rc.bottom();
        while ly < y1 {
            let mut lx = rc.x as i32;
            let x1 = rc.right();
            while lx < x1 {
                let old = p.get(lx, ly);
                p.put(lx, ly, old ^ style.fg);
                lx += 1;
            }
            ly += 1;
        }
        return;
    }
    if style.has(GUI_STYLE_DITHER50) {
        let mut ly = rc.y as i32;
        let y1 = rc.bottom();
        while ly < y1 {
            let mut lx = rc.x as i32;
            let x1 = rc.right();
            while lx < x1 {
                let c = if (lx + ly) & 1 == 0 { style.fg } else { style.bg };
                p.put(lx, ly, c);
                lx += 1;
            }
            ly += 1;
        }
        return;
    }
    p.fill_solid(rc, style.bg);
}

/// 1px 枠 (契約 G2)。`style.fg`。`DOTTED` = 点線、`XOR` = 反転。
pub fn draw_rect(surface: SurfaceId, rect: Rect, style: Style) {
    if rect.is_empty() {
        return;
    }
    let x = rect.x as i32;
    let y = rect.y as i32;
    let w = rect.w as i32;
    let h = rect.h as i32;
    hline(surface, x, y, w, style);
    if h > 1 {
        hline(surface, x, y + h - 1, w, style);
    }
    if h > 2 {
        let inner_h = h - 2;
        vline(surface, x, y + 1, inner_h, style);
        if w > 1 {
            vline(surface, x + w - 1, y + 1, inner_h, style);
        }
    }
}

/* ================================================================ */
/*  線                                                              */
/* ================================================================ */

/// 水平線 (契約 G2)。`style.fg`。長さ `w`。`DOTTED` / `XOR` 対応。
pub fn hline(surface: SurfaceId, x: i32, y: i32, w: i32, style: Style) {
    if w <= 0 {
        return;
    }
    let t = match clip::resolve_target(surface) {
        Some(t) => t,
        None => return,
    };
    let rc = clip_rect(&t, Rect::new(x as i16, y as i16, w as i16, 1));
    if rc.is_empty() {
        return;
    }
    let p = Painter::from_target(&t);
    let plain = !style.has(GUI_STYLE_DOTTED) && !style.has(GUI_STYLE_XOR);
    if plain {
        p.fill_solid(rc, style.fg);
        return;
    }
    let mut lx = rc.x as i32;
    let x1 = rc.right();
    let ly = rc.y as i32;
    while lx < x1 {
        span_pixel(&p, lx, ly, lx, &style);
        lx += 1;
    }
}

/// 垂直線 (契約 G2)。`style.fg`。長さ `h`。`DOTTED` / `XOR` 対応。
pub fn vline(surface: SurfaceId, x: i32, y: i32, h: i32, style: Style) {
    if h <= 0 {
        return;
    }
    let t = match clip::resolve_target(surface) {
        Some(t) => t,
        None => return,
    };
    let rc = clip_rect(&t, Rect::new(x as i16, y as i16, 1, h as i16));
    if rc.is_empty() {
        return;
    }
    let p = Painter::from_target(&t);
    let plain = !style.has(GUI_STYLE_DOTTED) && !style.has(GUI_STYLE_XOR);
    if plain {
        p.fill_solid(rc, style.fg);
        return;
    }
    let lx = rc.x as i32;
    let mut ly = rc.y as i32;
    let y1 = rc.bottom();
    while ly < y1 {
        span_pixel(&p, lx, ly, ly, &style);
        ly += 1;
    }
}

/// 点線/XOR を考慮した 1 画素 (parity は座標和で決める)。
#[inline]
fn span_pixel(p: &Painter, lx: i32, ly: i32, parity_src: i32, style: &Style) {
    if style.has(GUI_STYLE_DOTTED) && (parity_src & 1) != 0 {
        return; /* 点線: 1 個おき */
    }
    if style.has(GUI_STYLE_XOR) {
        let old = p.get(lx, ly);
        p.put(lx, ly, old ^ style.fg);
    } else {
        p.put(lx, ly, style.fg);
    }
}

/// 任意直線 (契約 G2)。`style.fg`。Bresenham を 1 画素ごとにクリップ判定して描く
/// (クリップ外へ 1 画素も出さない)。`DOTTED` / `XOR` 対応。
pub fn line(surface: SurfaceId, x0: i32, y0: i32, x1: i32, y1: i32, style: Style) {
    let t = match clip::resolve_target(surface) {
        Some(t) => t,
        None => return,
    };
    if t.clip.is_empty() {
        return;
    }
    let p = Painter::from_target(&t);
    let mut x = x0 as i32;
    let mut y = y0 as i32;
    let xe = x1 as i32;
    let ye = y1 as i32;
    let dx = (xe - x).abs();
    let dy = -(ye - y).abs();
    let sx = if x < xe { 1 } else { -1 };
    let sy = if y < ye { 1 } else { -1 };
    let mut err = dx + dy;
    let mut step: i32 = 0;
    loop {
        span_pixel(&p, x, y, step, &style);
        if x == xe && y == ye {
            break;
        }
        let e2 = 2 * err;
        if e2 >= dy {
            err += dy;
            x += sx;
        }
        if e2 <= dx {
            err += dx;
            y += sy;
        }
        step += 1;
    }
}

/* ================================================================ */
/*  ビットマップ転送 (契約 G2、カラーキー = 色 255)                  */
/* ================================================================ */

/// `bitmap` (オフスクリーンサーフェス) の `src_rect` を `(dx,dy)` へ転送する。
/// カラーキー 255 は転送しない。dst は画面サーフェスのみ (v1)。
pub fn blit(surface: SurfaceId, dx: i32, dy: i32, bitmap: SurfaceId, src_rect: Rect) {
    let t = match clip::resolve_target(surface) {
        Some(t) => t,
        None => return,
    };
    if t.offscreen {
        debug_assert!(false, "blit destination must be a screen surface in v1");
        return;
    }
    let s = st();
    let bidx = match s.resolve(bitmap) {
        Some(i) => i,
        None => return,
    };
    let src = match s.surfaces[bidx].kind {
        SurfaceKind::Offscreen { surf } => surf,
        SurfaceKind::Screen { .. } => return,
    };
    if src.is_null() {
        return;
    }

    /* src_rect を source surface 境界に収める (レビュー ②)。切り落とした左上ぶん
     * dx/dy も動かす。これをしないと gfx_blit_colorkey が source の前後を OOB read し、
     * CPL3 では #PF でアプリが kill され得る。 */
    let sw = s.surfaces[bidx].w as i32;
    let sh = s.surfaces[bidx].h as i32;
    let srx = src_rect.x as i32;
    let sry = src_rect.y as i32;
    let sx0 = if srx < 0 { 0 } else { srx };
    let sy0 = if sry < 0 { 0 } else { sry };
    let sx1 = core::cmp::min(srx + src_rect.w as i32, sw);
    let sy1 = core::cmp::min(sry + src_rect.h as i32, sh);
    if sx1 <= sx0 || sy1 <= sy0 {
        return;
    }
    let cdx = dx + (sx0 - srx);
    let cdy = dy + (sy0 - sry);
    let cw = sx1 - sx0;
    let ch = sy1 - sy0;

    /* dst 矩形をクリップ。src 原点をクリップのぶんだけずらす。 */
    let dst_full = Rect::new(cdx as i16, cdy as i16, cw as i16, ch as i16);
    let dst = clip_rect(&t, dst_full);
    if dst.is_empty() {
        return;
    }
    let off_x = dst.x as i32 - cdx;
    let off_y = dst.y as i32 - cdy;
    let sr = ffi::GfxRect {
        x: sx0 + off_x,
        y: sy0 + off_y,
        w: dst.w as i32,
        h: dst.h as i32,
    };
    unsafe {
        ffi::gfx_blit_colorkey(t.ox + dst.x as i32, t.oy + dst.y as i32, src, &sr, 255);
    }
}

/* ================================================================ */
/*  文字 (UTF-8 → 字形 engine の 1bpp mask、欠字・失敗は KCG)       */
/* ================================================================ */
//
// T7b-b4 S5 (設計 t7bb34 §2-2・§3-1): 全字を同じ mask 経路で描く。
// - 幅は `decode_glyph` (= `textcore::cols_at`): ASCII = 8px、Latin-1・Latin 拡張
//   (U+00A0..=U+024F) = 8px (T7b-b5、PM 決定)、他の多バイト = 16px、不正バイトは 8px。
// - 経路: ASCII 0x20..=0x7E のうち **0x5C (¥) 以外**は TTF (`Cell::Half`)、Latin-1・
//   Latin 拡張は TTF (`Cell::Half` — Inconsolata だけ、東雲の全角の字形は使わない)、
//   他の多バイトは TTF (`Cell::Full`)。0x5C・U+00A5・制御字・不正バイトは ROM の ANK
//   (`kcg_read_ank(byte)`、U+00A5 も 8px の cell)。engine が使えない・欠字・raster 失敗は
//   KCG (ASCII は `kcg_read_ank(byte)`、多バイトは `unicode_to_ank` → `kcg_read_ank`、
//   0 なら `unicode_to_jis` (0 なら 0x2222) → `kcg_read_kanji`。半角の Latin はその
//   16×16 を横 1/2 に詰めて 8px の cell に入れる)。
// - TTF の字は `cell.y + ascent - top` から (face ごとの baseline)、KCG は cell の
//   先頭行から。**cell 矩形 (8|16 × 16) の外の画素は描かない**、その上で Painter の
//   clip で画素単位に切る (全角もオフスクリーンも同じ)。
// - engine は text() 1 回につき `begin` (face ごとの検証 1 回) 〜 `end`。cache に無い字
//   だけ raster する (整数だけ)。bitmap の借用は 1 字の間 (`release`)。

/// ROM の ¥ (ANK 0x5C)。Inconsolata の `\` は描かない (設計 P3-4)。
const ANK_YEN: u8 = 0x5C;
/// U+00A5 YEN SIGN。ROM の ¥ (ANK 0x5C) で描く。
const CP_YEN: u32 = 0xA5;
/// JIS に無い字の代わり (□、gfx_kcg.c と同じ)。
const JIS_FALLBACK: u16 = 0x2222;

/// 1 cell の 1bpp (行ごと u16、MSB = cell の左端、半角は上位 8 bit だけ)。
pub(crate) type CellRows = [u16; CELL_H as usize];

/// 1 コードポイントを取り出す。戻り: (バイト長, ASCII か, セル幅px)。末尾で不完全なら len=0。
fn decode_glyph(bytes: &[u8]) -> (usize, bool, i32) {
    if bytes.is_empty() {
        return (0, true, 0);
    }
    let n = textcore::seq_len(bytes[0]);
    if n == 1 {
        return (1, true, ANK_W); /* ASCII、継続/不正バイト: 1 個スキップ、半角扱い */
    }
    if bytes.len() < n {
        return (0, false, 0);
    }
    /* 幅の規則は textcore の 1 本 (edit_gui の桁・折り返しと同じ実体)。 */
    (n, false, textcore::cols_at(bytes) as i32 * ANK_W)
}

/// 多バイト列 (`decode_glyph` が長さ 2..=4 と判定したもの) のコードポイント。
fn decode_cp(seq: &[u8]) -> u32 {
    textcore::decode_cp(seq)
}

/// 1 字の行き先。
#[derive(Clone, Copy, PartialEq, Eq)]
enum Route {
    /// ROM の ANK (0x5C・U+00A5・制御字・不正バイト)。
    Ank(u8),
    /// 字形 engine。KCG に落ちたら `ank` (ASCII) か `cp` の KCG。
    Ttf { cp: u32, cell: FontCell, ank: u8 },
}

fn route(seq: &[u8], is_ank: bool) -> Route {
    let b = seq[0];
    if is_ank {
        if (0x20..=0x7E).contains(&b) && b != ANK_YEN {
            return Route::Ttf { cp: u32::from(b), cell: FontCell::Half, ank: b };
        }
        return Route::Ank(b);
    }
    let cp = decode_cp(seq);
    if cp == CP_YEN {
        /* P3-4: U+00A5 も ROM の ¥。東雲16 は JIS X 0201 の名残で U+00A5 に `\` の
         * 字形を持つ (U+005C が ¥ の形) ので TTF に回さない。gshell は
         * unicode_to_ank → 0x5C で同じ ROM ¥ になる。cell 幅は 8px (T7b-b5)。 */
        return Route::Ank(ANK_YEN);
    }
    /* T7b-b5: Latin-1・Latin 拡張は半角 (Inconsolata)。幅は decode_glyph が同じ規則で 8px。 */
    let cell = if textcore::cp_is_half(cp) { FontCell::Half } else { FontCell::Full };
    Route::Ttf { cp, cell, ank: 0 }
}

/// ROM の ANK 8×16 (cell の先頭行から)。
fn kcg_ank(ank: u8) -> CellRows {
    let mut pat = [0u8; CELL_H as usize];
    unsafe { (os32api::api().kcg_read_ank)(ank, pat.as_mut_ptr()) };
    let mut rows: CellRows = [0; CELL_H as usize];
    for (r, b) in pat.iter().enumerate() {
        rows[r] = u16::from(*b) << 8;
    }
    rows
}

/// ROM の多バイト字: ANK に写る字 (半角カナ・全角英数・U+00A5) は ANK 8×16、
/// それ以外は漢字 16×16 (JIS に無ければ □)。gfx_kcg.c の kcg_draw_utf8 と同じ。
fn kcg_cp(cp: u32) -> CellRows {
    let ank = unsafe { ffi::unicode_to_ank(cp) };
    if ank != 0 {
        return kcg_ank(ank);
    }
    let mut jis = unsafe { (os32api::api().unicode_to_jis)(cp) };
    if jis == 0 {
        jis = JIS_FALLBACK;
    }
    let mut pat = [0u8; CELL_H as usize * 2];
    unsafe { (os32api::api().kcg_read_kanji)(jis, pat.as_mut_ptr()) };
    let mut rows: CellRows = [0; CELL_H as usize];
    for (r, row) in rows.iter_mut().enumerate() {
        *row = (u16::from(pat[2 * r]) << 8) | u16::from(pat[2 * r + 1]);
    }
    rows
}

/// 16×16 を横 1/2 の 8×16 に詰める (隣り合う 2 列の OR)。半角の Latin が TTF で
/// 描けないとき (欠字・KCG だけの設定) の ROM の字 (T7b-b5、gshell の text.rs と同じ)。
pub(crate) fn squeeze_half(full: &CellRows) -> CellRows {
    let mut rows: CellRows = [0; CELL_H as usize];
    for (r, out) in rows.iter_mut().enumerate() {
        let v = full[r];
        let mut c = 0;
        while c < 8 {
            if v & (0xC000 >> (2 * c)) != 0 {
                *out |= 0x8000 >> c;
            }
            c += 1;
        }
    }
    rows
}

/// engine の bitmap を cell (幅 `w`、16 行) に置く: 左上は (`left`, `ascent - top`)、
/// cell の外へ出る行・列は捨てる (G2 の cell 契約)。
pub(crate) fn cell_from_glyph(g: &Glyph, bits: &[u8], w: i32) -> CellRows {
    let mut rows: CellRows = [0; CELL_H as usize];
    let e = &g.entry;
    let pitch = usize::from(e.pitch);
    if pitch == 0 || pitch > 4 {
        return rows;
    }
    let oy = i32::from(g.ascent) - i32::from(e.top);
    let ox = i32::from(e.left);
    let wmask: u16 = if w >= KANJI_W { 0xFFFF } else { 0xFF00 };
    let mut r = 0usize;
    while r < usize::from(e.height) {
        let dy = oy + r as i32;
        if dy >= 0 && dy < CELL_H {
            let mut v: u32 = 0;
            let mut k = 0;
            while k < pitch {
                let b = bits.get(r * pitch + k).copied().unwrap_or(0);
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

/// TTF の 1 字を cell の mask に。描けなければ None (呼び手が KCG)。
/// cache に無い字だけ raster する (整数だけ、F6 で fp_scope を撤去)。
fn engine_cell(e: &mut AppEngine, cp: u32, cell: FontCell, w: i32) -> Option<CellRows> {
    let mut g = Glyph::default();
    let mut r = e.peek(cp, PixelSize::Px16, &mut g);
    if r == Resolved::Miss {
        r = e.resolve(cp, cell, PixelSize::Px16, &mut g);
    }
    match r {
        Resolved::Bitmap => {
            let rows = cell_from_glyph(&g, e.bitmap(&g), w);
            e.release();
            Some(rows)
        }
        Resolved::Space => Some([0; CELL_H as usize]),
        Resolved::Kcg | Resolved::Missing | Resolved::Miss => None,
    }
}

/// 1 cell を置く: 不透過は cell∩clip を `bg` で塗り、`fg` の画素を Painter の clip で。
fn put_cell(p: &Painter, penx: i32, ly: i32, w: i32, rows: &CellRows, fg: u8, bg: Option<u8>) {
    if let Some(b) = bg {
        p.fill_solid(Rect::new(penx as i16, ly as i16, w as i16, CELL_H as i16), b);
    }
    let mut row = 0i32;
    while row < CELL_H {
        let bits = rows[row as usize];
        if bits != 0 {
            let r = p.row(ly + row);
            let mut col = 0i32;
            while col < w {
                if bits & (0x8000 >> col) != 0 {
                    match &r {
                        Some(r) => r.put(penx + col, fg),
                        None => p.put(penx + col, ly + row, fg),
                    }
                }
                col += 1;
            }
        }
        row += 1;
    }
}

/// UTF-8 文字列を `(x,y)` から描く (契約 G2)。半角 8px / 全角 16px の cell、字形は
/// 字形 engine (欠字・失敗は KCG)。clip は画素単位、不透過は cell を塗ってから、
/// `TRANSPARENT_BG` は fg の画素だけ。戻り値: 送り幅 px (`measure_text` と同じ)。
pub fn text(surface: SurfaceId, x: i32, y: i32, utf8: &[u8], style: Style) -> i32 {
    let t = match clip::resolve_target(surface) {
        Some(t) => t,
        None => return 0,
    };
    let p = Painter::from_target(&t);
    let bg = if style.has(GUI_STYLE_TRANSPARENT_BG) { None } else { Some(style.bg) };

    /* text() 1 回につき face ごとの検証 1 回 (§2-4)。使えなければ全字 KCG。 */
    let mut eng = crate::font::ensure_open();
    let usable = match eng.as_mut() {
        Some(e) => e.begin(),
        None => false,
    };

    let ly = y;
    let mut penx = x;
    let mut i = 0usize;
    while i < utf8.len() {
        let (len, is_ank, cw) = decode_glyph(&utf8[i..]);
        if len == 0 {
            break; /* 末尾の不完全シーケンス: UTF-8 境界で切る */
        }
        if utf8[i] == 0 {
            break; /* NUL 終端も尊重 */
        }
        /* cell が clip に掛からなければ字を引かない (raster もしない)。 */
        if !p.cell_hidden(penx, ly, cw, CELL_H) {
            let rows = match route(&utf8[i..i + len], is_ank) {
                Route::Ank(b) => kcg_ank(b),
                Route::Ttf { cp, cell, ank } => {
                    let drawn = match eng.as_mut() {
                        Some(e) if usable => engine_cell(e, cp, cell, cw),
                        _ => None,
                    };
                    match drawn {
                        Some(rows) => rows,
                        None if ank != 0 => kcg_ank(ank),
                        None if cw == ANK_W => squeeze_half(&kcg_cp(cp)),
                        None => kcg_cp(cp),
                    }
                }
            };
            put_cell(&p, penx, ly, cw, &rows, style.fg, bg);
        }
        penx += cw;
        i += len;
    }
    if let Some(e) = eng {
        e.end();
    }
    penx - x
}

/// レイアウト用の寸法 (契約 G2)。半角 8px (ASCII・Latin-1/拡張) / 全角 16px、高さ 16px (scale1)。
pub fn measure_text(utf8: &[u8]) -> (i32, i32) {
    let mut w = 0i32;
    let mut i = 0usize;
    while i < utf8.len() {
        if utf8[i] == 0 {
            break;
        }
        let (len, _ank, cw) = decode_glyph(&utf8[i..]);
        if len == 0 {
            break;
        }
        w += cw;
        i += len;
    }
    (w, CELL_H)
}

/* ================================================================ */
/*  能力とカウンタ (契約 G5 / G7)                                    */
/* ================================================================ */

/// 画面能力 (KAPI v40 `gfx_screen_info`)。GUI とアプリはこれを信じ、決め打ちしない。
pub fn screen_info() -> ScreenInfo {
    // Existing entry reports no drawable screen after a failed attach.
    if unsafe { core::ptr::read_volatile(core::ptr::addr_of!(ffi::gfx_ready)) } == 0 { return ScreenInfo::ZERO; }
    crate::gstate::screen_info_cached()
}

/// GUI カウンタ (KAPI v41 `gfx_stats`)。累積。NP21/W では転送量で性能を見積もる。
pub fn stats() -> Stats {
    let mut out = Stats::ZERO;
    unsafe {
        let a = os32api::api();
        (a.gfx_stats)(&mut out as *mut Stats as *mut u8);
    }
    out
}

/// このライブラリが基底未設定の窓面描画を拒んだ累計 (テスト観測用)。
pub fn base_violation_count() -> u32 {
    st().base_violations
}
