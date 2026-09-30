/*
 * gdi_test — G 描画 API (票 C1) の目視確認 + ゲート G1 用テストプログラム
 *
 * 契約 G の G API **だけ** で描く (libos32gfx を直接叩かない):
 *   - G6 の 16 色見本 (役割名つき)
 *   - Style.flags 4 種 (TRANSPARENT_BG / XOR / DOTTED / DITHER50) の見本
 *   - 文字 (ANK / 漢字 / 混在)
 *   - クリップの入れ子 (push_clip / pop_clip)
 *   - stats() の表示 (全画面 1 present で present_bytes を確認)
 *
 * crt0 から main(argc, argv, api) として呼ばれる。
 */
#![no_std]
#![no_main]

extern crate os32api;
extern crate libos32gui;

use libos32gui::gapi as g;
use os32api::gui::proto::*;
use os32api::gui::types::{Rect, Style, SurfaceId};
use os32api::KernelAPI;

fn r(x: i32, y: i32, w: i32, h: i32) -> Rect {
    Rect::new(x as i16, y as i16, w as i16, h as i16)
}

/* u32 → 10 進 ASCII。戻り値: 桁数。 */
fn u32_to_dec(mut v: u32, buf: &mut [u8]) -> usize {
    if v == 0 {
        buf[0] = b'0';
        return 1;
    }
    let mut tmp = [0u8; 12];
    let mut n = 0;
    while v > 0 && n < tmp.len() {
        tmp[n] = b'0' + (v % 10) as u8;
        v /= 10;
        n += 1;
    }
    let mut i = 0;
    while i < n {
        buf[i] = tmp[n - 1 - i];
        i += 1;
    }
    n
}

/* 固定長文字列ビルダ (no_std / no_alloc)。 */
struct Buf {
    b: [u8; 96],
    n: usize,
}
impl Buf {
    fn new() -> Buf {
        Buf { b: [0; 96], n: 0 }
    }
    fn s(&mut self, bytes: &[u8]) {
        for &c in bytes {
            if self.n < self.b.len() {
                self.b[self.n] = c;
                self.n += 1;
            }
        }
    }
    fn u(&mut self, v: u32) {
        let mut d = [0u8; 12];
        let k = u32_to_dec(v, &mut d);
        self.s(&d[..k]);
    }
    fn as_slice(&self) -> &[u8] {
        &self.b[..self.n]
    }
}

/* 役割名 (index 順、G6)。40px 幅に入る短縮名。 */
static ROLE: [&[u8]; 16] = [
    b"TEXT", b"TITLE_ACT", b"SHADOW", b"DISABLED", b"OK", b"WARN", b"FACE", b"WINDOW", b"CLOSE",
    b"LINK", b"ACCENT", b"LIGHT", b"DESKTOP", b"HILIGHT", b"SEL_TXT", b"EDIT_BG",
];

/* 暗い色 (白文字が要る index)。 */
fn is_dark(idx: usize) -> bool {
    matches!(idx, 0 | 1 | 2 | 3 | 8 | 9 | 12 | 13)
}

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    os32api::os32_init(api);

    unsafe {
        let a = os32api::api();

        /* GFX モード初期化 (400 ライン)。libos32gfx を通す。 */
        os32api::gfx::libos32gfx_init(api);

        /* G6 の 16 色を実機パレットへ (gshell が本来やる。単体テストなので自前で入れる)。 */
        let mut i = 0;
        while i < 16 {
            let c = GUI_SYSTEM_PALETTE[i];
            (a.gfx_set_palette)(i as i32, c.r, c.g, c.b);
            i += 1;
        }
    }

    /* 画面能力を信じる (640×400 を決め打ちしない、G5)。 */
    let info = g::screen_info();
    let sw = info.width as i32;
    let sh = info.height as i32;

    /* フルスクリーンサーフェス + 基底クリップ = 画面全体 (Paint 相当)。 */
    let screen: SurfaceId = g::screen_surface();
    g::set_base_clip(screen, r(0, 0, sw, sh));

    /* 背景 = DESKTOP。 */
    g::fill_rect(screen, r(0, 0, sw, sh), Style::new(0, GUI_COLOR_DESKTOP));

    /* タイトル。 */
    g::fill_rect(screen, r(0, 0, sw, 20), Style::new(0, GUI_COLOR_TITLE_ACTIVE));
    g::text(
        screen,
        6,
        2,
        b"gdi_test: G API 16-color / styles / text / clip",
        Style::new(GUI_COLOR_TITLE_TEXT, GUI_COLOR_TITLE_ACTIVE),
    );

    draw_swatches(screen);
    draw_style_samples(screen);
    draw_text_samples(screen);
    draw_clip_demo(screen);
    draw_edge_clip_checks(screen, sw, sh);

    /* --- 全画面を 1 回だけ present。present_bytes を測る基準 (H1)。 --- */
    unsafe {
        let a = os32api::api();
        (a.gfx_add_dirty_rect)(0, 0, sw, sh);
        (a.gfx_present_dirty)();
    }

    /* この時点の stats を読む = 全画面 1 present ぶん (期待 present_bytes=128000)。 */
    let s1 = g::stats();

    /* tvram にも出す (機械可読。ゲートはここを読む)。 */
    unsafe {
        let a = os32api::api();
        (a.kprintf)(
            0x07,
            b"[gdi_test] present_bytes=%d commits=%d hw_ops=%d io=%d\r\n\0".as_ptr(),
            s1.present_bytes as i32,
            s1.commits as i32,
            s1.hw_ops as i32,
            s1.io_accesses as i32,
        );
    }

    /* 画面にも stats を描く (screenshot 参照用)。stats 行だけを present するので
     * present_bytes への追加はごくわずか (全画面ではない)。 */
    let mut b = Buf::new();
    b.s(b"stats present_bytes=");
    b.u(s1.present_bytes);
    b.s(b" commits=");
    b.u(s1.commits);
    let line_y = sh - 16;
    g::fill_rect(screen, r(0, line_y, sw, 16), Style::new(0, GUI_COLOR_WINDOW));
    g::text(screen, 6, line_y, b.as_slice(), Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW));
    unsafe {
        let a = os32api::api();
        (a.gfx_add_dirty_rect)(0, line_y, sw, 16);
        (a.gfx_present_dirty)();
    }

    /* キー待ち → テキスト画面復帰。 */
    unsafe {
        let a = os32api::api();
        (a.kbd_getchar)();
        (a.gfx_shutdown)();
        (a.tvram_clear)();
    }
    0
}

/* ---- 16 色見本帯 ---- */
fn draw_swatches(screen: SurfaceId) {
    let y = 24;
    let sw_w = 40;
    let sw_h = 40;
    let mut idx = 0usize;
    while idx < 16 {
        let x = (idx as i32) * sw_w;
        /* 塗り (bg = そのインデックス色)。 */
        g::fill_rect(screen, r(x, y, sw_w, sw_h), Style::new(0, idx as u8));
        g::draw_rect(screen, r(x, y, sw_w, sw_h), Style::pen(GUI_COLOR_TEXT));
        /* index 番号を見本の上に。暗い色は白文字。 */
        let tc = if is_dark(idx) { GUI_COLOR_WINDOW } else { GUI_COLOR_TEXT };
        let mut d = [0u8; 12];
        let k = u32_to_dec(idx as u32, &mut d);
        g::text(screen, x + 4, y + 2, &d[..k], Style::new(tc, idx as u8));
        idx += 1;
    }

    /* 役割名の凡例 (4 列 × 4 行、白地に黒文字)。 */
    let ly = y + sw_h + 4;
    g::fill_rect(screen, r(0, ly, 640, 4 * 16), Style::new(0, GUI_COLOR_WINDOW));
    let mut i = 0usize;
    while i < 16 {
        let col = (i / 4) as i32;
        let row = (i % 4) as i32;
        let x = col * 160 + 4;
        let ty = ly + row * 16;
        let mut b = Buf::new();
        b.u(i as u32);
        b.s(b" ");
        b.s(ROLE[i]);
        g::text(screen, x, ty, b.as_slice(), Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW));
        i += 1;
    }
}

/* ---- Style.flags 4 種 ---- */
fn draw_style_samples(screen: SurfaceId) {
    let y = 148;
    let bw = 150;
    let bh = 48;
    let gap = 8;

    /* 見出し。 */
    g::text(screen, 6, y - 16, b"Style.flags:", Style::new(GUI_COLOR_WINDOW, GUI_COLOR_DESKTOP));

    /* 1) TRANSPARENT_BG: 色地の上に、背景を塗らず文字だけ。 */
    let x0 = 6;
    g::fill_rect(screen, r(x0, y, bw, bh), Style::new(0, GUI_COLOR_LINK));
    g::text(
        screen,
        x0 + 6,
        y + 8,
        b"TRANSPARENT",
        Style::new(GUI_COLOR_WINDOW, 0).with_flags(GUI_STYLE_TRANSPARENT_BG),
    );
    g::text(
        screen,
        x0 + 6,
        y + 26,
        b"only glyphs",
        Style::new(GUI_COLOR_TEXT, 0).with_flags(GUI_STYLE_TRANSPARENT_BG),
    );

    /* 2) XOR: 面を塗ってから XOR で反転帯を重ねる。 */
    let x1 = x0 + bw + gap;
    g::fill_rect(screen, r(x1, y, bw, bh), Style::new(0, GUI_COLOR_FACE));
    g::fill_rect(
        screen,
        r(x1 + 10, y + 10, bw - 20, bh - 20),
        Style::new(0x0F, 0).with_flags(GUI_STYLE_XOR),
    );
    g::text(screen, x1 + 6, y + 2, b"XOR", Style::new(GUI_COLOR_TEXT, GUI_COLOR_FACE));

    /* 3) DOTTED: 点線の枠 (フォーカス矩形風)。 */
    let x2 = x1 + bw + gap;
    g::fill_rect(screen, r(x2, y, bw, bh), Style::new(0, GUI_COLOR_WINDOW));
    g::draw_rect(
        screen,
        r(x2 + 6, y + 6, bw - 12, bh - 12),
        Style::pen(GUI_COLOR_TEXT).with_flags(GUI_STYLE_DOTTED),
    );
    g::text(screen, x2 + 10, y + 16, b"DOTTED", Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW));

    /* 4) DITHER50: 市松 (fg=黒 / bg=白 → グレー)。 */
    let x3 = x2 + bw + gap;
    g::fill_rect(
        screen,
        r(x3, y, bw, bh),
        Style::new(GUI_COLOR_TEXT, GUI_COLOR_WINDOW).with_flags(GUI_STYLE_DITHER50),
    );
    g::draw_rect(screen, r(x3, y, bw, bh), Style::pen(GUI_COLOR_TEXT));
    g::text(
        screen,
        x3 + 8,
        y + 16,
        b"DITHER50",
        Style::new(GUI_COLOR_TEXT, 0).with_flags(GUI_STYLE_TRANSPARENT_BG),
    );
}

/* ---- 文字 (ANK / 漢字 / 混在) ---- */
fn draw_text_samples(screen: SurfaceId) {
    let y = 206;
    g::text(
        screen,
        6,
        y,
        b"ANK: The quick brown fox 0123456789 !#$%&",
        Style::new(GUI_COLOR_WINDOW, GUI_COLOR_DESKTOP),
    );
    /* 漢字 / 混在 (JIS テーブル未準備なら□になり得るが無害)。 */
    g::text(
        screen,
        6,
        y + 18,
        "混在: OS32 日本語テキスト ABC 漢字".as_bytes(),
        Style::new(GUI_COLOR_WINDOW, GUI_COLOR_DESKTOP),
    );
}

/* ---- クリップの入れ子 ---- */
fn draw_clip_demo(screen: SurfaceId) {
    let cx = 340;
    let cy = 148;
    let cw = 290;
    let ch = 48;

    g::text(screen, cx, cy - 16, b"clip nesting:", Style::new(GUI_COLOR_WINDOW, GUI_COLOR_DESKTOP));

    /* クリップ境界を枠で示す。 */
    g::draw_rect(screen, r(cx, cy, cw, ch), Style::pen(GUI_COLOR_CLOSE));

    /* 外側クリップ = この枠の内側。 */
    g::push_clip(r(cx + 1, cy + 1, cw - 2, ch - 2));
    /* 枠より大きく塗ろうとしても外へ出ない。 */
    g::fill_rect(screen, r(cx - 40, cy - 40, cw + 80, ch + 80), Style::new(0, GUI_COLOR_OK));
    /* はみ出す対角線 (クリップされる)。 */
    g::line(
        screen,
        cx - 30,
        cy - 30,
        cx + cw + 30,
        cy + ch + 30,
        Style::pen(GUI_COLOR_TEXT),
    );
    /* 内側クリップを入れ子に。別の色で塗る。 */
    g::push_clip(r(cx + 40, cy + 10, 120, ch - 20));
    g::fill_rect(screen, r(cx - 40, cy - 40, cw + 80, ch + 80), Style::new(0, GUI_COLOR_ACCENT));
    g::text(
        screen,
        cx + 44,
        cy + 14,
        b"nested-clip-text-overflow",
        Style::new(GUI_COLOR_WINDOW, GUI_COLOR_ACCENT),
    );
    g::pop_clip();
    g::pop_clip();
}

/* ---- 画面端 4 辺のクリップ確認 (サーフェス外へ 1px も出さない) ---- */
fn draw_edge_clip_checks(screen: SurfaceId, sw: i32, sh: i32) {
    let c = Style::new(0, GUI_COLOR_WARN);
    /* 左上をまたぐ塗り。 */
    g::fill_rect(screen, r(-12, -12, 24, 24), c);
    /* 右下をまたぐ塗り。 */
    g::fill_rect(screen, r(sw - 12, sh - 12, 24, 24), c);
    /* 左端をまたぐ文字 (ANK 画素単位クリップ)。 */
    g::text(screen, -12, 268, b"EDGE-LEFT-CLIP", Style::new(GUI_COLOR_WINDOW, GUI_COLOR_DESKTOP));
    /* 右端をまたぐ文字。 */
    g::text(screen, sw - 40, 286, b"EDGE-RIGHT-CLIP", Style::new(GUI_COLOR_WINDOW, GUI_COLOR_DESKTOP));
}
