//! text_tests.rs — T7b-b3 S3: gshell の文字描画 (src/text.rs) のホスト TDD。
//!
//! 描画口は `mocks::gfx_draw_font` (呼出しを記録し、画素も置く)。KCG は
//! `kcg_read_ank` / `kcg_read_kanji` の贋物 (0xa5 埋め、引いた字は `kcg_reads`)。
//! 見るもの: 経路と幅の規則 (¥・半角カナ・FF21・制御字)、KCG fallback、
//! cell と clip の画素単位の切り方、不透過/透過、2 倍、実 font の baseline と cell 契約。
use super::*;
use crate::mocks::{self, fontsvc, FontDraw};
use crate::wm::{self, Rect};
use os32_font::engine::{Cell, Glyph};

const FULL: Rect = Rect { x: 0, y: 0, w: 640, h: 400 };

fn kcg_only() {
    mocks::init(); /* 表が無い = engine は閉じたまま = 全部 KCG */
}

fn with_fonts() -> wm::GuiState {
    mocks::init();
    let st = wm::GuiState::NEW;
    fontsvc::install(crate::ffi::SHELL_SERVICE_VERSION);
    assert_eq!(crate::font::open_at_safe_point(&st, 0), 0);
    mocks::clear(9);
    st
}

fn draws() -> Vec<FontDraw> {
    mocks::font_draws()
}

#[test]
fn t01_route_keeps_the_kcg_width_rules() {
    kcg_only();
    assert_eq!(route('A' as u32), Route::Ttf { cp: 0x41, cell: Cell::Half, ank: 0x41 });
    assert_eq!(route(0xFF21), Route::Ttf { cp: 0x41, cell: Cell::Half, ank: 0x41 },
               "全角英字 Ａ は 'A' と同じ entry (8px)");
    assert_eq!(route(0x5C), Route::Ank(0x5C), "0x5C は ROM の ¥");
    assert_eq!(route(0xA5), Route::Ank(0x5C), "U+00A5 も ROM の ¥");
    assert_eq!(route(0xFF71), Route::Ank(0xB1), "半角カナは ROM");
    assert_eq!(route(0x3042), Route::Ttf { cp: 0x3042, cell: Cell::Full, ank: 0 });
    assert_eq!(route(0x7E), Route::Ttf { cp: 0x7E, cell: Cell::Half, ank: 0x7E });
    assert_eq!(route(0x01), Route::Skip);
    assert_eq!(route(0xFEFF), Route::Skip);
    assert_eq!(route(0x09), Route::Tab);
    assert_eq!(route(0x0A), Route::Newline);
    let cps: Vec<_> = ttf_glyphs("A¥\tあ\0B".as_bytes()).collect();
    assert_eq!(cps, vec![(0x41, Cell::Half), (0x3042, Cell::Full)], "NUL で止まる・ANK は除く");
}

#[test]
fn t02_kcg_fallback_draws_cells_and_advances_like_kcg_draw_utf8() {
    kcg_only();
    let w = draw(FULL, 10, 20, "A¥あ一\tB\nC".as_bytes(), 1, 0);
    assert_eq!(
        mocks::kcg_reads(),
        vec![('a', 0x41), ('a', 0x5C), ('k', 0x2422), ('k', 0x2222), ('a', 0x42), ('a', 0x43)],
        "ANK は kcg_read_ank、全角は unicode_to_jis → kcg_read_kanji (無ければ 0x2222)"
    );
    let d = draws();
    let xy: Vec<_> = d.iter().map(|x| (x.x, x.y, x.w_bytes, x.h_lines)).collect();
    assert_eq!(xy, vec![(10, 20, 1, 16), (18, 20, 1, 16), (26, 20, 2, 16), (42, 20, 2, 16),
                        (90, 20, 1, 16), (10, 38, 1, 16)],
               "8/16px の送り、\\t は +32、\\n は行頭へ 18px 下");
    assert_eq!(w, 8, "戻りは最後の行の幅 (kcg_draw_utf8 と同じ)");
    assert!(d.iter().all(|x| x.pat.iter().all(|b| *b == 0xa5)), "ROM の字形をそのまま置く");
}

#[test]
fn t03_clip_cuts_rows_by_pointer_and_columns_by_mask() {
    kcg_only();
    /* 全角 1 字 (16×16) を列 3..13・行 5..11 だけ見せる。 */
    let clip = Rect::new(103, 205, 10, 6);
    draw_t(clip, 100, 200, "あ".as_bytes(), 1);
    let d = draws();
    assert_eq!(d.len(), 1);
    assert_eq!((d[0].x, d[0].y, d[0].w_bytes, d[0].h_lines), (100, 205, 2, 6),
               "行は clip の上端から clip の高さだけ");
    /* 0xa5a5 の列 3..13 だけ: 1010 0101 1010 0101 & 0001 1111 1111 1000。 */
    for r in 0..6 {
        assert_eq!(&d[0].pat[r * 2..r * 2 + 2], &[0x05, 0xA0], "列の mask (行 {r})");
    }
    /* clip に掛からない字は描かない。 */
    let before = draws().len();
    draw_t(Rect::new(0, 0, 10, 10), 100, 200, b"A", 1);
    assert_eq!(draws().len(), before);
}

#[test]
fn t04_opaque_fills_cell_and_clip_only_transparent_does_not() {
    kcg_only();
    mocks::clear(9);
    draw(Rect::new(0, 0, 4, 8), 0, 0, b"A", 1, 7);
    let p = mocks::pixels();
    let at = |x: usize, y: usize| p[y * mocks::W + x];
    /* cell∩clip = 4×8。0xa5 の画素は fg、残りは bg、外は触らない。 */
    assert_eq!((at(0, 0), at(1, 0), at(2, 0), at(3, 0)), (1, 7, 1, 7));
    assert_eq!(at(4, 0), 9, "clip の外を塗った");
    assert_eq!(at(0, 8), 9, "clip の外を塗った");
    kcg_only();
    mocks::clear(9);
    draw_t(Rect::new(0, 0, 4, 8), 0, 0, b"A", 1);
    let p = mocks::pixels();
    assert_eq!((p[0], p[1]), (1, 9), "透過で背景を塗った");
}

#[test]
fn t05_x2_doubles_each_pixel_and_cell() {
    kcg_only();
    let w = draw_x2(FULL, 0, 0, "Aあ".as_bytes(), 1, None);
    assert_eq!(w, 48);
    let d = draws();
    assert_eq!((d[0].w_bytes, d[0].h_lines, d[1].x, d[1].w_bytes, d[1].h_lines), (2, 32, 16, 4, 32));
    /* 0xa5 → 1100 1100 0011 0011 → 0xCC33。 */
    assert_eq!(&d[0].pat[..4], &[0xCC, 0x33, 0xCC, 0x33]);
}

#[test]
fn t06_cell_from_glyph_places_by_ascent_and_drops_outside_the_cell() {
    let mut g = Glyph::default();
    g.ascent = 13;
    g.entry.top = 3; /* 上端は行 10 */
    g.entry.left = -2; /* 左 2 列は cell の外 */
    g.entry.width = 12;
    g.entry.height = 8; /* 行 10..18: 16, 17 は cell の外 */
    g.entry.pitch = 2;
    let bits = [0xFF, 0xF0].repeat(8);
    let rows = cell_from_glyph(&g, &bits, 8);
    for r in 0..16 {
        let want = if (10..16).contains(&r) { 0xFF00 } else { 0 };
        assert_eq!(rows[r], want, "行 {r}");
    }
    /* 全角 cell は 16 列まで: 12 - 2 = 10 列。 */
    let rows = cell_from_glyph(&g, &bits, 16);
    assert_eq!(rows[10], 0xFFC0);
    /* 右へずらすと cell の右端で切れる。 */
    g.entry.left = 6;
    let rows = cell_from_glyph(&g, &bits, 8);
    assert_eq!(rows[10], 0x0300);
}

#[test]
fn t07_real_fonts_stay_in_their_cells_and_use_per_face_baselines() {
    let _st = with_fonts();
    /* 欧文 'g' は Inconsolata (ascent 13): 下端は cell の最終行まで。和文 '亜' は東雲。 */
    let w = draw_t(FULL, 0, 0, "gH亜".as_bytes(), 1);
    assert_eq!(w, 32);
    assert!(mocks::kcg_reads().is_empty(), "標準 set の字を KCG で描いた");
    let p = mocks::pixels();
    let ink = |x0: usize, x1: usize| -> Vec<usize> {
        (0..40).filter(|y| (x0..x1).any(|x| p[y * mocks::W + x] == 1)).collect()
    };
    let g = ink(0, 8);
    let wide = ink(8, 16); /* 'H' */
    let kanji = ink(16, 32);
    assert!(!g.is_empty() && !wide.is_empty() && !kanji.is_empty());
    assert!(*g.last().unwrap() <= 15 && *wide.last().unwrap() <= 15 && *kanji.last().unwrap() <= 15,
            "cell (16 行) の外に描いた");
    assert!(*g.last().unwrap() > *wide.last().unwrap(), "g の descender が baseline の下に無い");
    /* 'H' (平らな底) の最下行は ascent 13 の 1 つ上 = 行 12、'g' は 3 行下まで。 */
    assert_eq!(*wide.last().unwrap(), 12);
    assert_eq!(*g.last().unwrap(), 15);
    /* 列も cell の外に出ない (隣の cell を汚さない)。 */
    assert!((0..40).all(|y| p[y * mocks::W + 32] != 1));
}

#[test]
fn t08_missing_glyph_in_both_faces_goes_to_kcg() {
    let _st = with_fonts();
    /* 私用領域は両 face に無い → Missing → KCG の □ (0x2222)。 */
    draw(FULL, 0, 0, "\u{E000}".as_bytes(), 1, 0);
    assert_eq!(mocks::kcg_reads(), vec![('k', 0x2222)]);
    /* 2 回目は cache の fallback entry (read も raster もしない)。 */
    let reads = fontsvc::with(|f| f.reads);
    draw(FULL, 0, 0, "\u{E000}".as_bytes(), 1, 0);
    assert_eq!(fontsvc::with(|f| f.reads), reads);
}

#[test]
fn t09_draw_ank_reads_the_byte_as_ank_not_unicode() {
    /* FEP の w==1 cell (S6): byte は ANK。0x5C は ROM の ¥、半角カナも ROM、
     * 0x20..=0x7E の残りは draw と同じ TTF の entry、0x20 未満は送らない。 */
    kcg_only();
    assert_eq!(draw_ank(FULL, 0, 0, 0x5C, 1, 0), 8);
    assert_eq!(draw_ank(FULL, 8, 0, 0xB1, 1, 0), 8);
    assert_eq!(draw_ank(FULL, 16, 0, b'A', 1, 0), 8);
    assert_eq!(draw_ank(FULL, 24, 0, 0x1B, 1, 0), 0);
    assert_eq!(mocks::kcg_reads(), vec![('a', 0x5C), ('a', 0xB1), ('a', 0x41)],
               "KCG のみでは全部 ROM の ANK (0xB1 を U+00B1 の漢字にしない)");
    let xy: Vec<_> = draws().iter().map(|d| (d.x, d.y, d.w_bytes, d.h_lines)).collect();
    assert_eq!(xy, vec![(0, 0, 1, 16), (8, 0, 1, 16), (16, 0, 1, 16)], "8px の cell、制御字は描かない");

    let _st = with_fonts();
    draw_ank(FULL, 0, 0, b'A', 1, 0);
    draw_ank(FULL, 8, 0, 0x5C, 1, 0);
    draw_ank(FULL, 16, 0, 0xB1, 1, 0);
    assert_eq!(mocks::kcg_reads(), vec![('a', 0x5C), ('a', 0xB1)],
               "'A' は Inconsolata、¥ (0x5C) と半角カナは ROM (Inconsolata の \\ は描かない)");
    let ank_a = mocks::pixels();
    mocks::clear(9);
    draw(FULL, 0, 0, b"A", 1, 0);
    let p = mocks::pixels();
    assert!((0..16).all(|y| (0..8).all(|x| p[y * mocks::W + x] == ank_a[y * mocks::W + x])),
            "draw_ank の 'A' が draw の 'A' と違う");
}
