//! T7b-a1 corpus limits for IPAex 004.01 and Inconsolata Regular 3.000.
//! Points/contours/components/input count the expanded glyph, including repeats.
//! Depth: simple glyph = 0. Bbox is before cell fitting, at 16/32px.
//! Exceeding these limits will select KCG/replacement in the later reader.

pub const MAX_POINTS: usize = 512;
pub const MAX_CONTOURS: usize = 32;
pub const MAX_COMPONENTS: usize = 8;
pub const MAX_COMPONENT_DEPTH: usize = 4;
pub const MAX_GLYPH_SPAN_BYTES: usize = 2 * 1024;
pub const MAX_TOTAL_INPUT_BYTES: usize = 4 * 1024;
pub const MAX_EDGES: usize = 512;
// Public outline commands include implicit on-curve midpoints and closing points.
pub const MAX_OUTLINE_POINTS: usize = 2 * MAX_POINTS + MAX_CONTOURS;
pub const MAX_GLYPH_WORK_BYTES: usize = 32 * 1024;
pub const MAX_BBOX_PX: usize = 40;
pub const MAX_TABLES: usize = 32;
// Encoding records (not segments/groups); supported originals have at most 4.
pub const MAX_CMAP_SUBTABLES: usize = 32;
pub const MAX_CMAP_SEGMENTS: usize = 8192;
pub const MAX_CMAP_GROUPS: usize = 16384;
// Tune the fixed LRU capacity here; no heap allocation or whole-file residency.
// T7b-a6 (NP21/W, JF Dot Shinonome 16, format 4 with 4,119 segments): 2x2KiB
// thrashed between the cmap arrays (open = 16,479 reads, 29 s); 16x1KiB opens in
// 39 reads (0.13 s) and keeps 1,000 sequential glyphs at 188 reads. A read costs
// ~1.1 ms at 1KiB and ~1.8 ms at 2KiB there, so slots stay 1KiB. 16KiB per face.
// Evidence: ~/os32-tmp/evidence/2026-10-20/t7ba6/RESULT.md.
pub const READ_CACHE_SLOTS: usize = 16;
pub const READ_CACHE_SLOT_BYTES: usize = 1024;
// T7b-a7 resident cmap: the selected subtable's search arrays (format 4: 8 B per
// segment + 2, format 12: 12 B per group) are copied at open when they fit here;
// glyphIdArray stays on read_at. Measured: JF Dot Shinonome 16 format 4 with 4,119
// segments = 32,954 B, Inconsolata 133 segments = 1,066 B (both resident).
// IPAex 004.01 selects format 12 with 10,718 groups = 128,616 B (its format 4 would
// be 39,970 B) and takes the read_at path. Every face reserves this many bytes.
pub const MAX_CMAP_RESIDENT_BYTES: usize = 34 * 1024;
// T7b-a5 raster: the outline is rasterised once at RASTER_PPEM (unhinted,
// FreeType-compatible 26.6 point scaling, exact area coverage); 32px is that 1bpp bitmap doubled (nearest neighbour).
pub const RASTER_PPEM: u16 = 16;
pub const MAX_PIXEL_FACTOR: usize = 2;
// Largest committed bitmap (1bpp, byte-aligned rows) at MAX_PIXEL_FACTOR.
pub const MAX_BITMAP_BYTES: usize =
    (MAX_BBOX_PX * MAX_PIXEL_FACTOR).div_ceil(8) * MAX_BBOX_PX * MAX_PIXEL_FACTOR;
// TASK_FONTPERF F5 integer raster (fontperf design §6): bitmap-relative points
// in 1/RASTER_SUBPIXEL px (26.6 x 64); one pixel of area/cover accumulates to
// RASTER_AREA_ONE = 2 * RASTER_SUBPIXEL^2 (ftgrays-style), reported as
// coverage 0..=COVERAGE_ONE. A pixel is black when area / RASTER_AREA_ONE >=
// COVERAGE_BLACK_NUM / COVERAGE_BLACK_DEN, tested exactly in integers. 128/255
// is the a5 threshold (FONTCHK and the EBDT equality were accepted with it);
// the design's 128/256 (FreeType's 8-bit >= 128) would turn every pixel at
// exactly 0.5 black (F5 measured: IPAex Gothic 2,217 glyphs / 7,853 px,
// Inconsolata 18 / 40 px of the corpora); the PM kept 128/255 (FONTPERF design, F5 decisions). 1/4096 px keeps
// the integer coverage within the old float raster's threshold decisions
// (1/256 px flipped 194 IPAex glyphs near 128/255, 1/4096 px 11); the products
// stay in i32 (one pixel = 2^25) / i64.
pub const RASTER_SUBPIXEL_BITS: u32 = 12;
pub const RASTER_SUBPIXEL: i32 = 1 << RASTER_SUBPIXEL_BITS;
pub const RASTER_AREA_ONE: u64 = 2 * (RASTER_SUBPIXEL as u64) * (RASTER_SUBPIXEL as u64);
pub const COVERAGE_BITS: u32 = 8;
pub const COVERAGE_ONE: i32 = 1 << COVERAGE_BITS;
pub const COVERAGE_BLACK_NUM: u64 = 128;
pub const COVERAGE_BLACK_DEN: u64 = 255;
// Outline coordinates (font units) beyond this are rejected before the 26.6
// products (which then fit i64/i32).
pub const MAX_COORD_UNITS: i32 = 1_048_576;
// a1 quadratic flattening: n segments with n^4 >= QUAD_FLATNESS_SQ * |a - 2c + b|^2
// (in px^2; error <= 1/8 px). The subpixel form is n^4 * RASTER_SUBPIXEL^2 >= 4 d2.
pub const QUAD_FLATNESS_SQ: u64 = 4;

// T7b-b3 glyph cache (glyph cache design §3, b3/b4 design §2-1) with the
// TASK_FONTPERF F2 packed cell class (fontperf design §3). One owner arena of
// fixed slots in three classes, in this order:
//  - Cell: CELL_SLOT_BYTES = an 8 B header + the 16-row cell bitmap (Full 16x16
//    pitch 2, Half 8x16 pitch 1 in the first 16 B). 16px bitmaps of a face with
//    an ascent, clipped to the cell (the drawing contract drops the rest; F-15
//    (c)), and 16px Space/Kcg/Missing (0 B).
//  - Big16: SLOT16_BYTES = SLOT_HDR_BYTES + 20x21; 16px bitmaps without a cell
//    position (no face ascent), or every 16px key in a layout without cells.
//  - Big32: SLOT32_BYTES = SLOT_HDR_BYTES + 36x37; every 32px key (N32 = 0: uncached).
// The big slot rectangle is the standard face's measured maximum plus margin
// (re-measure in a5 when the face changes). A glyph that does not fit is drawn uncached.
pub const SLOT_HDR_BYTES: usize = 32;
pub const SLOT16_W: usize = 20;
pub const SLOT16_H: usize = 21;
pub const SLOT32_W: usize = 36;
pub const SLOT32_H: usize = 37;
pub const fn slot_bytes(w: usize, h: usize) -> usize {
    (SLOT_HDR_BYTES + w.div_ceil(8) * h).div_ceil(8) * 8
}
pub const SLOT16_BYTES: usize = slot_bytes(SLOT16_W, SLOT16_H);
pub const SLOT32_BYTES: usize = slot_bytes(SLOT32_W, SLOT32_H);
const _: () = assert!(SLOT16_BYTES == 96 && SLOT32_BYTES == 224);
// Cell class: cell width 16 (Full) or 8 (Half), CELL_H_PX rows.
pub const CELL_FULL_W: usize = 16;
pub const CELL_HALF_W: usize = 8;
pub const CELL_HDR_BYTES: usize = 8;
pub const CELL_BITMAP_BYTES: usize = CELL_FULL_W / 8 * CELL_H_PX;
pub const CELL_SLOT_BYTES: usize = CELL_HDR_BYTES + CELL_BITMAP_BYTES;
const _: () = assert!(CELL_SLOT_BYTES == 40);
// Arena per owner (fontperf design §5): gshell's is a static (.bss) array, the
// app's one LARGE mem_alloc returned on trim (256KiB is decided in F4).
pub const ARENA_BYTES_SHELL: usize = 128 * 1024;
pub const ARENA_BYTES_APP: usize = 128 * 1024;
// Big16 slots are fixed; the rest of the arena is cells. N32 stays 0 (32px is the
// 16px bitmap doubled, drawn uncached). The layout changes only on attach.
pub const N16: usize = 32;
pub const N32: usize = 0;
/// (n_cell, n16) for an arena of `arena_bytes` (n32 is N32).
pub const fn layout(arena_bytes: usize) -> (usize, usize) {
    let big = N16 * SLOT16_BYTES + N32 * SLOT32_BYTES;
    let n_cell = if arena_bytes > big { (arena_bytes - big) / CELL_SLOT_BYTES } else { 0 };
    (n_cell, N16)
}
const _: () = assert!(layout(64 * 1024).0 == 1_561);
const _: () = assert!(layout(ARENA_BYTES_SHELL).0 == 3_200 && layout(ARENA_BYTES_APP).0 == 3_200);
const _: () = assert!(layout(256 * 1024).0 == 6_476);
// Hash buckets (power of two) holding u16 slot numbers (chains 1.5-3.1 at 1,561-3,200 cells).
pub const BUCKETS: usize = 1024;
const _: () = assert!(BUCKETS.is_power_of_two());
// Static context (buckets/clock state/stats) budget beside the arena.
pub const CACHE_STATIC_BYTES: usize = 4 * 1024;
// Value of the `font_set` setting.
pub const FONT_SET_STD: u16 = 0;
pub const FONT_SET_KCG_ONLY: u16 = 1;
// T7b-b5 (PM decision): Latin-1 Supplement and Latin Extended-A/B are half-width
// (8px cell, latin face only, KCG when the latin face lacks the glyph). Greek,
// Cyrillic and every other non-ASCII code point stay full-width. U+00A5 is in the
// range but both shells draw it from the ROM yen (ANK 0x5C, P3-4).
pub const LATIN_HALF_FIRST: u32 = 0xA0;
pub const LATIN_HALF_LAST: u32 = 0x24F;
// Rows above the baseline in a 16-row cell (U1 (a): per-face baseline). 16ppem:
// Shinonome ascent 13.75 (14 rows above, 2 below); Inconsolata ASCII yMax 11.76 '(',
// yMin -2.78 'j' (12 above, 3 below). Asserted against the fonts by check-font-engine-host.
pub const CELL_H_PX: usize = 16;
pub const ASCENT_JA_PX: i16 = 14;
pub const ASCENT_LATIN_PX: i16 = 13;
