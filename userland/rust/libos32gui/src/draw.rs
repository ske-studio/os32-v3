//! G 描画プリミティブ (契約 G2) — libos32gfx の上に載せる。
//!
//! **鉄則 (票 C1 / v2 C8)**:
//! - 描画の実体は libos32gfx を FFI で呼ぶ。プレーンへ直接ピクセルを書かない
//!   (PLANAR4)。線・枠はクライアント側の CPU 実装 (幾何) だが、点は `gfx_pixel`
//!   (libos32gfx) を通す。8bpp (`GFX_FMT_PACKED8`) だけは契約どおり C1 の CPU 実装で
//!   線形バッファへ直接書く分岐を用意する (v1.1 前半は PLANAR4 のみ動けばよい)。
//! - **クリップは自分で守る**: サーフェス外へは 1 ピクセルも出さない。矩形は交差で、
//!   線は 1 ピクセルごとにクリップ判定で、文字はグリフセル単位 (ANK は画素単位) で守る。
//! - ステートレス: 呼び出しごとに `Style` を受ける。DC 相当の選択状態は持たない。
use crate::clip::{self, Target};
use crate::ffi;
use crate::gstate::{screen_info_cached, st, SurfaceKind};
use crate::surface::offscreen_ptr;
use os32api::gui::proto::{
    GUI_STYLE_DITHER50, GUI_STYLE_DOTTED, GUI_STYLE_TRANSPARENT_BG, GUI_STYLE_XOR,
};
use os32api::gui::types::{Rect, ScreenInfo, Stats, Style, SurfaceId, GFX_FMT_PACKED8};

const CELL_H: i32 = 16; /* KCG セル高 (scale1) */
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
        let info = screen_info_cached();
        let packed8 = !t.offscreen && info.format == GFX_FMT_PACKED8;
        let (fb_base, fb_pitch) = if packed8 {
            let fb = unsafe { ffi::gfx_fb };
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
/*  文字 (UTF-8 → KCG)                                              */
/* ================================================================ */

/// 1 コードポイントを取り出す。戻り: (バイト長, ASCII か, セル幅px)。末尾で不完全なら len=0。
fn decode_glyph(bytes: &[u8]) -> (usize, bool, i32) {
    if bytes.is_empty() {
        return (0, true, 0);
    }
    let b0 = bytes[0];
    if b0 < 0x80 {
        (1, true, ANK_W)
    } else if b0 & 0xE0 == 0xC0 {
        if bytes.len() >= 2 { (2, false, KANJI_W) } else { (0, false, 0) }
    } else if b0 & 0xF0 == 0xE0 {
        if bytes.len() >= 3 { (3, false, KANJI_W) } else { (0, false, 0) }
    } else if b0 & 0xF8 == 0xF0 {
        if bytes.len() >= 4 { (4, false, KANJI_W) } else { (0, false, 0) }
    } else {
        (1, true, ANK_W) /* 継続/不正バイト: 1 個スキップ、半角扱い */
    }
}

/// UTF-8 文字列を `(x,y)` から KCG で描く (契約 G2)。半角 8px / 全角 16px。
/// クリップはグリフセル単位 (ANK かつ `TRANSPARENT_BG` は画素単位)。戻り値: 送り幅 px。
pub fn text(surface: SurfaceId, x: i32, y: i32, utf8: &[u8], style: Style) -> i32 {
    let t = match clip::resolve_target(surface) {
        Some(t) => t,
        None => return 0,
    };
    let p = Painter::from_target(&t);
    unsafe { ffi::kcg_set_scale(1) };

    let transparent = style.has(GUI_STYLE_TRANSPARENT_BG);
    let ly = y as i32;
    let mut penx = x as i32;
    let mut i = 0usize;
    while i < utf8.len() {
        let (len, is_ank, cw) = decode_glyph(&utf8[i..]);
        if len == 0 {
            break; /* 末尾の不完全シーケンス: UTF-8 境界で切る */
        }
        if utf8[i] == 0 {
            break; /* NUL 終端も尊重 */
        }
        let cell = Rect::new(penx as i16, ly as i16, cw as i16, CELL_H as i16);

        if is_ank && transparent {
            /* ANK 透過: グリフビットマップを読み、fg 画素だけを画素単位クリップで置く */
            draw_ank_bitmap(&p, penx, ly, utf8[i], style.fg, None);
        } else if is_ank {
            /* ANK 不透過: ビットマップで画素単位クリップ (端でも欠けない) */
            draw_ank_bitmap(&p, penx, ly, utf8[i], style.fg, Some(style.bg));
        } else {
            /* 全角: セルがクリップ内に完全に収まるときだけ 1 グリフ描く
             * (kcg_draw_utf8 が UTF-8→JIS 変換と描画を担う)。透過は v1 では不透過に退避。 */
            if !p.offscreen.is_null() {
                /* オフスクリーンへの全角は kcg 経路が無い (v1 制限) */
            } else if t.clip.contains_rect(&cell) {
                let mut buf = [0u8; 5];
                let n = len.min(4);
                buf[..n].copy_from_slice(&utf8[i..i + n]);
                buf[n] = 0;
                unsafe {
                    ffi::kcg_draw_utf8(p.ox + penx, p.oy + ly, buf.as_ptr(), style.fg, style.bg);
                }
            }
        }

        penx += cw;
        i += len;
    }
    penx - x as i32
}

/// ANK (8x16) グリフをビットマップから画素単位で描く。`bg=None` は透過 (fg 画素だけ)。
/// ビットは MSB が左端と仮定 (kcg_read_ank のパック規約)。
fn draw_ank_bitmap(p: &Painter, penx: i32, ly: i32, ch: u8, fg: u8, bg: Option<u8>) {
    if !p.offscreen.is_null() && bg.is_some() {
        /* オフスクリーンの不透過 ANK は塗ってから... だが read_ank のビット順に依存するため
         * v1 では画面サーフェスに限定。オフスクリーン文字は未対応 (票 C1 の割り切り)。 */
    }
    /* セルごとクリップの外なら何もしない (行 × 画素の判定を 1 回に畳む)。 */
    if p.cell_hidden(penx, ly, ANK_W, CELL_H) {
        return;
    }
    /* ANK は 8x16 = 16B。念のため広めに確保して先頭 16B だけ使う。 */
    let mut pat = [0u8; 32];
    unsafe {
        let a = os32api::api();
        (a.kcg_read_ank)(ch, pat.as_mut_ptr());
    }
    /* 不透過ならセルをまとめて塗ってから前景ビットだけ置く。結果は
     * 「画素ごとに fg / bg を選ぶ」のと同じで、背景側が memset になる。 */
    if let Some(b) = bg {
        p.fill_solid(Rect::new(penx as i16, ly as i16, ANK_W as i16, CELL_H as i16), b);
    }
    let mut row = 0i32;
    while row < CELL_H {
        let bits = pat[row as usize];
        if bits != 0 {
            match p.row(ly + row) {
                Some(r) => {
                    let mut col = 0i32;
                    while col < ANK_W {
                        if (bits >> (7 - col)) & 1 != 0 {
                            r.put(penx + col, fg);
                        }
                        col += 1;
                    }
                }
                None => {
                    let mut col = 0i32;
                    while col < ANK_W {
                        if (bits >> (7 - col)) & 1 != 0 {
                            p.put(penx + col, ly + row, fg);
                        }
                        col += 1;
                    }
                }
            }
        }
        row += 1;
    }
}

/// レイアウト用の寸法 (契約 G2)。半角 8px / 全角 16px、高さ 16px (scale1)。
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
    crate::gstate::refresh_screen_info()
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
