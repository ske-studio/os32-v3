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
pub const READ_CACHE_SLOTS: usize = 2;
pub const READ_CACHE_SLOT_BYTES: usize = 2 * 1024;
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
