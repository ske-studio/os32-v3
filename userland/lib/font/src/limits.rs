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
// Pixel is black when its 8-bit-equivalent coverage is >= 128.
pub const COVERAGE_THRESHOLD: f32 = 128.0 / 255.0;
// Outline coordinates (font units) beyond this (or NaN) are rejected before
// integer casts; 26.6 products then fit i64/i32.
pub const MAX_COORD_UNITS: f32 = 1_048_576.0;
// a1 quadratic flattening: n segments with n^4 >= this * |a - 2c + b|^2 (error <= 1/8 px).
pub const QUAD_FLATNESS_SQ: f32 = 4.0;

// T7b-b3 glyph cache (glyph cache design §3, b3/b4 design §2-1). One owner
// arena of fixed slots per pixel class: a SLOT_HDR_BYTES header followed by the
// 1bpp bitmap (byte-aligned rows). The slot rectangle is the standard face's
// measured maximum plus margin (re-measure in a5 when the face changes; JF Dot
// Shinonome 16 at 16px is 16x16). A glyph that does not fit is drawn uncached.
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
pub const ARENA_BYTES: usize = 65536;
// Standard set is 16px only (32px draws the 16px bitmap doubled): the whole arena
// is 16px slots. N16/N32 change only right after invalidate_all (cache design T2).
pub const N16: usize = 682;
pub const N32: usize = 0;
const _: () = assert!(N16 * SLOT16_BYTES + N32 * SLOT32_BYTES <= ARENA_BYTES);
// Hash buckets (power of two) holding u16 slot numbers.
pub const BUCKETS: usize = 256;
const _: () = assert!(BUCKETS.is_power_of_two());
// Static context (buckets/state/stats) budget beside the arena (cache design §1).
pub const CACHE_STATIC_BYTES: usize = 2 * 1024;
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
