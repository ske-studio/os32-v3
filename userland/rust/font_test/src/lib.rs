/*
 * font_test — IPAフォント TrueType レンダリングテスト (Rust)
 *
 * ttf-parser (zero-alloc) + ab_glyph_rasterizer でオンデマンドラスタライズ。
 * fontdue と違い、全グリフを一括パースしないためメモリ効率が良い。
 *
 * フォントファイル: /data/ipaexg.ttf (ゲスト側パス、明朝は /data/ipaexm.ttf)。
 * 日本語 OpenType は配布物に同梱しない (ユーザー決定 2026-09-30) — 使う人が
 * assets/fonts/ipaexg.ttf (無改変の原本) を hsync などで /data に置く。
 * 無ければ open 失敗を報告して終了する (GFX モードには入らない)。
 * 以前の JIS X 0208 サブセット (ipaexg_subset.ttf) は廃止した。
 */
#![no_std]
#![no_main]

extern crate alloc;
extern crate os32api;

use alloc::vec;
use alloc::vec::Vec;
use ab_glyph_rasterizer::{point, Rasterizer};
use ttf_parser::Face;
use os32api::gfx;
use os32api::kprint;
use os32api::KernelAPI;

/* フォントファイルのゲスト側パス */
const FONT_PATH: &[u8] = b"/data/ipaexg.ttf\0";
const FONT_PATH_MINCHO: &[u8] = b"/data/ipaexm.ttf\0";

/// フォントファイルが開けるか (中身は読まない)。無いときに GFX モードへ
/// 入らず報告して終わるための前検査。
fn font_available(path: &[u8]) -> bool {
    unsafe {
        let a = os32api::api();
        let fd = (a.sys_open)(path.as_ptr(), os32api::fs::O_RDONLY);
        if fd < 0 {
            return false;
        }
        (a.sys_close)(fd);
        true
    }
}

/* PC-98 画面サイズ */
const SCREEN_W: i32 = 640;
const SCREEN_H: i32 = 400;

/* ================================================================ */
/*  OutlineBuilder → ab_glyph_rasterizer ブリッジ                   */
/* ================================================================ */

struct GlyphBuilder {
    rasterizer: Rasterizer,
    scale: f32,
    off_x: f32,
    off_y: f32,
    first_x: f32,
    first_y: f32,
    last_x: f32,
    last_y: f32,
}

impl GlyphBuilder {
    fn tx(&self, x: f32) -> f32 { x * self.scale + self.off_x }
    fn ty(&self, y: f32) -> f32 { -y * self.scale + self.off_y }
}

impl ttf_parser::OutlineBuilder for GlyphBuilder {
    fn move_to(&mut self, x: f32, y: f32) {
        let px = self.tx(x);
        let py = self.ty(y);
        self.first_x = px;
        self.first_y = py;
        self.last_x = px;
        self.last_y = py;
    }

    fn line_to(&mut self, x: f32, y: f32) {
        let px = self.tx(x);
        let py = self.ty(y);
        self.rasterizer.draw_line(
            point(self.last_x, self.last_y),
            point(px, py),
        );
        self.last_x = px;
        self.last_y = py;
    }

    fn quad_to(&mut self, x1: f32, y1: f32, x: f32, y: f32) {
        let (cx, cy) = (self.tx(x1), self.ty(y1));
        let (px, py) = (self.tx(x), self.ty(y));
        self.rasterizer.draw_quad(
            point(self.last_x, self.last_y),
            point(cx, cy),
            point(px, py),
        );
        self.last_x = px;
        self.last_y = py;
    }

    fn curve_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x: f32, y: f32) {
        let (c1x, c1y) = (self.tx(x1), self.ty(y1));
        let (c2x, c2y) = (self.tx(x2), self.ty(y2));
        let (px, py) = (self.tx(x), self.ty(y));
        self.rasterizer.draw_cubic(
            point(self.last_x, self.last_y),
            point(c1x, c1y),
            point(c2x, c2y),
            point(px, py),
        );
        self.last_x = px;
        self.last_y = py;
    }

    fn close(&mut self) {
        if self.last_x != self.first_x || self.last_y != self.first_y {
            self.rasterizer.draw_line(
                point(self.last_x, self.last_y),
                point(self.first_x, self.first_y),
            );
        }
        self.last_x = self.first_x;
        self.last_y = self.first_y;
    }
}

/* ================================================================ */
/*  グリフラスタライズ                                              */
/* ================================================================ */

struct GlyphBitmap {
    width: usize,
    height: usize,
    bearing_x: i32,
    bearing_y: i32,
    advance: i32,
    bitmap: Vec<u8>,
}

/// 1文字をラスタライズして α ビットマップを返す
fn rasterize_glyph(face: &Face, ch: char, size: f32) -> Option<GlyphBitmap> {
    let glyph_id = face.glyph_index(ch)?;
    let bbox = face.glyph_bounding_box(glyph_id)?;

    let upem = face.units_per_em() as f32;
    let scale = size / upem;

    /* ピクセル寸法 — マージンを十分に取り、グリフ上端が切れるのを防止 */
    let w = ((bbox.x_max - bbox.x_min) as f32 * scale) as usize + 4;
    let h = ((bbox.y_max - bbox.y_min) as f32 * scale) as usize + 4;
    if w < 2 || h < 2 { return None; }

    /* ラスタライザ座標オフセット: TTF座標 → ピクセル座標 */
    let off_x = -(bbox.x_min as f32) * scale + 1.0;
    let off_y = (bbox.y_max as f32) * scale + 1.0;

    let mut builder = GlyphBuilder {
        rasterizer: Rasterizer::new(w, h),
        scale, off_x, off_y,
        first_x: 0.0, first_y: 0.0,
        last_x: 0.0, last_y: 0.0,
    };

    face.outline_glyph(glyph_id, &mut builder)?;

    /* α値ビットマップ取得 */
    let mut bitmap = vec![0u8; w * h];
    builder.rasterizer.for_each_pixel_2d(|x, y, alpha| {
        let idx = y as usize * w + x as usize;
        if idx < bitmap.len() {
            let a = (alpha * 255.0) as u32;
            bitmap[idx] = if a > 255 { 255 } else { a as u8 };
        }
    });

    let advance = face.glyph_hor_advance(glyph_id)
        .unwrap_or(upem as u16) as f32 * scale;

    Some(GlyphBitmap {
        width: w,
        height: h,
        bearing_x: (bbox.x_min as f32 * scale) as i32 - 1,
        bearing_y: (bbox.y_max as f32 * scale) as i32 + 1,
        advance: advance as i32,
        bitmap,
    })
}

/* ================================================================ */
/*  ビットマップ描画                                                */
/* ================================================================ */

fn draw_bitmap(
    x: i32, y: i32,
    width: usize, height: usize,
    bitmap: &[u8],
    fg: u8, _dim: u8,
) {
    unsafe {
        for row in 0..height {
            for col in 0..width {
                let px = x + col as i32;
                let py = y + row as i32;
                if px < 0 || px >= SCREEN_W || py < 0 || py >= SCREEN_H { continue; }

                /* 1bit モノクロ: 閾値96 で ON/OFF */
                let alpha = bitmap[row * width + col];
                if alpha >= 96 {
                    gfx::gfx_pixel(px, py, fg);
                }
            }
        }
    }
}

/* ================================================================ */
/*  UTF-8 文字列描画                                                */
/* ================================================================ */

fn draw_string(
    face: &Face,
    x: i32, y: i32,
    text: &str,
    size: f32,
    fg: u8, dim: u8,
) -> i32 {
    let upem = face.units_per_em() as f32;
    let scale = size / upem;
    let ascender = face.ascender() as f32 * scale;

    let mut cx = x;
    let mut cy = y;

    for ch in text.chars() {
        if ch == '\n' {
            cx = x;
            cy += size as i32 + 2;
            continue;
        }

        /* スペースの場合はアドバンスだけ進める */
        if ch == ' ' {
            cx += (size * 0.3) as i32;
            continue;
        }

        match rasterize_glyph(face, ch, size) {
            Some(g) => {
                let gx = cx + g.bearing_x;
                let gy = cy + (ascender as i32 - g.bearing_y);
                draw_bitmap(gx, gy, g.width, g.height, &g.bitmap, fg, dim);
                cx += g.advance;
            }
            None => {
                /* グリフが見つからない場合はスキップ */
                cx += (size * 0.5) as i32;
            }
        }
    }

    cy + size as i32
}
/* ================================================================ */
/*  フォント読み込み＆3サイズ描画ヘルパー                           */
/* ================================================================ */

fn load_and_render(
    path: &[u8],
    label: &[u8],
    start_y: i32,
    color: u8,
) -> i32 {
    let font_data = match os32api::fs::read_file(path) {
        Some(d) => d,
        None => {
            kprint!(b"WARN: %s not found\r\n\0", path.as_ptr());
            return start_y;
        }
    };

    let face = match Face::parse(&font_data, 0) {
        Ok(f) => f,
        Err(_) => { return start_y; }
    };

    /* セクションラベル (KCGで描画) */
    unsafe {
        gfx::kcg_set_scale(1);
        gfx::kcg_draw_utf8(8, start_y, label.as_ptr(), color, 0);
    }

    let mut y = start_y + 18;

    /* 16px */
    y = draw_string(&face, 16, y,
        "16px: ABC \u{3042}\u{3044}\u{3046} \u{30A2}\u{30A4}\u{30A6} \u{6F22}\u{5B57}\u{30C6}\u{30B9}\u{30C8} \u{5927}\u{5B66} \u{60C5}\u{5831} \u{5DE5}\u{5B66}",
        16.0, 7, 0);
    y += 4;

    /* 24px */
    y = draw_string(&face, 16, y,
        "24px: \u{65E5}\u{672C}\u{8A9E}\u{30D5}\u{30A9}\u{30F3}\u{30C8} ABC",
        24.0, 7, 0);
    y += 6;

    /* 32px */
    y = draw_string(&face, 16, y,
        "32px: \u{6F22}\u{5B57} OS32",
        32.0, 7, 0);

    y + 8
}

/* ================================================================ */
/*  メイン関数                                                       */
/* ================================================================ */
#[no_mangle]
pub extern "C" fn main(
    _argc: i32,
    _argv: *const *const u8,
    api: *mut KernelAPI,
) -> i32 {
    os32api::os32_init(api);

    os32api::print(b"TrueType Font Test - 1bit Monochrome\r\n\0");
    os32api::print(b"Gothic + Mincho x 16/24/32px = 6 patterns\r\n\0");

    /* フォントは同梱していない。無ければここで終わる (GFX モードに入らない) */
    if !font_available(FONT_PATH) {
        os32api::print(
            b"font_test: SKIP font not deployed (/data/ipaexg.ttf)\r\n\0",
        );
        return 2;
    }

    os32api::print(b"Press any key to start...\r\n\0");
    os32api::wait_key();

    /* GFX モード初期化 */
    unsafe {
        gfx::libos32gfx_init(api);
        gfx::gfx_clear(0);
    }

    /* --- ヘッダー --- */
    unsafe {
        gfx::gfx_fill_rect(0, 0, SCREEN_W, 20, 1);
        gfx::kcg_set_scale(1);
        gfx::kcg_draw_utf8(
            8, 4,
            b"[1bit Monochrome] TrueType Font Test - Gothic / Mincho\0".as_ptr(),
            7, 1,
        );
    }

    /* --- ゴシック体 (読み込み → 描画 → スコープ終了で解放) --- */
    let y_mid = load_and_render(
        FONT_PATH,
        b"--- IPAex Gothic ---\0",
        24,
        15,
    );

    /* --- 明朝体 (ゴシックのデータは解放済み) --- */
    load_and_render(
        FONT_PATH_MINCHO,
        b"--- IPAex Mincho ---\0",
        y_mid,
        15,
    );

    /* VRAM転送 */
    gfx::present();

    /* キー入力待ち → テキストモードに復帰 */
    os32api::wait_key();
    gfx::shutdown();

    0
}

