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
pub const MAX_BBOX_PX: usize = 40;
pub const MAX_TABLES: usize = 32;
// Encoding records (not segments/groups); supported originals have at most 4.
pub const MAX_CMAP_SUBTABLES: usize = 32;
pub const MAX_CMAP_SEGMENTS: usize = 8192;
pub const MAX_CMAP_GROUPS: usize = 16384;
// Tune the fixed LRU capacity here; no heap allocation or whole-file residency.
pub const READ_CACHE_SLOTS: usize = 2;
pub const READ_CACHE_SLOT_BYTES: usize = 2 * 1024;
