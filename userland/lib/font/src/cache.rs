//! T7b-b3 glyph bitmap cache (glyph cache design §2-§4, b3/b4 design §2-1) with
//! the TASK_FONTPERF F2 packed cells (fontperf design §3).
//! One owner-private arena of fixed slots in three classes, slot numbers in this
//! order: Cell (8 B header + 16-row cell bitmap; 16px keys that fit the cell at
//! the face's ascent, and 16px Space/Kcg/Missing), Big16 (SLOT_HDR_BYTES header +
//! 20x21; 16px bitmaps that leave the cell) and Big32 (every 32px key). Bitmaps
//! are 1bpp, MSB left, byte-aligned rows, top to bottom; a Cell keeps the bitmap
//! in cell coordinates (row 0 = baseline - ascent, column 0 = pen) and answers
//! `{left 0, top ascent, width cell, height 16}`.
//! Lookup is by `(set, cp, px)` through a single-linked hash chain of slot
//! numbers. Placement per class: free slots are filled in order (`fill`), then a
//! clock (second chance) from `hand`: a set reference bit is cleared and passed,
//! a clear one (or an invalid slot) is the victim. A hit sets the bit, a commit
//! leaves it clear. Every change of face or setting goes through the one
//! `invalidate_all` (fill = hand = 0, every slot invalid and unreferenced); there
//! is no per-face invalidation and no age. No heap, no variable-size allocator,
//! no LRU links. The arena belongs to the caller (attach/detach); only complete
//! results are committed.
use crate::limits::*;
use crate::reader::{Error, Result};
use core::ops::Range;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GlyphKey {
    /// font_set_slot: the (latin, ja) face pair of one setting generation.
    pub set: u16,
    pub cp: u32,
    /// Raster pixel class: 16 or 32.
    pub px: u8,
}

/// Cell width of the glyph, decided by the caller's width rules: Half 8px,
/// Full 16px. The engine also uses it for the cmap search order.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Cell {
    Half,
    Full,
}
impl Cell {
    pub const fn width(self) -> usize {
        match self {
            Cell::Half => CELL_HALF_W,
            Cell::Full => CELL_FULL_W,
        }
    }
}

/// What a committed entry stands for. Kcg/Missing carry no bitmap (0 B); Space is
/// a complete, inkless result. Distinct from an invalid slot (header code 0).
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Kind {
    Bitmap = 1,
    Space = 2,
    /// Raster/read/limit failure: draw the KCG/replacement glyph.
    Kcg = 3,
    /// In neither face's cmap: draw the KCG/replacement glyph.
    #[default]
    Missing = 4,
}
impl Kind {
    fn from_code(code: u8) -> Option<Self> {
        match code {
            1 => Some(Kind::Bitmap),
            2 => Some(Kind::Space),
            3 => Some(Kind::Kcg),
            4 => Some(Kind::Missing),
            _ => None,
        }
    }
}

/// The face that produced an entry (drawn_face_slot); None for KCG/Missing
/// (and for every non-Bitmap Cell entry).
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum FaceId {
    #[default]
    None = 0,
    Latin = 1,
    Ja = 2,
}
impl FaceId {
    fn from_code(code: u16) -> Self {
        match code {
            1 => FaceId::Latin,
            2 => FaceId::Ja,
            _ => FaceId::None,
        }
    }
}

/// Rows above the baseline of a face's 16px cell (`ASCENT_*_PX`).
pub const fn ascent16(face: FaceId) -> Option<i16> {
    match face {
        FaceId::Latin => Some(ASCENT_LATIN_PX),
        FaceId::Ja => Some(ASCENT_JA_PX),
        FaceId::None => None,
    }
}

/// Committed placement: bbox top-left relative to the pen on the baseline
/// (`top` = rows above the baseline), bitmap size. No advance (G2 cell widths).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Entry {
    pub kind: Kind,
    pub face: FaceId,
    pub left: i16,
    pub top: i16,
    pub width: u16,
    pub height: u16,
    pub pitch: u16,
}
impl Entry {
    pub fn bytes(&self) -> usize {
        usize::from(self.pitch) * usize::from(self.height)
    }
}

/// Big slot header layout (cache design §2). Stored little-endian at the start
/// of every Big16/Big32 slot; `next` is the hash chain (slot number, NONE =
/// 0xffff). `age` is no longer used (stays 0); `valid` bit0 = valid, bit1 =
/// clock reference bit.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SlotHdr {
    pub codepoint: u32,
    pub age: u32,
    pub left: i16,
    pub top: i16,
    pub set: u16,
    pub face: u16,
    pub next: u16,
    pub width: u16,
    pub height: u16,
    pub pitch: u16,
    pub px: u8,
    pub kind: u8,
    pub valid: u8,
    pub pad: [u8; 5],
}
const _: () = assert!(core::mem::size_of::<SlotHdr>() == SLOT_HDR_BYTES);
const NONE: u16 = 0xffff;
// Big header field offsets (same order as SlotHdr).
const H_CP: usize = 0;
const H_AGE: usize = 4;
const H_LEFT: usize = 8;
const H_TOP: usize = 10;
const H_SET: usize = 12;
const H_FACE: usize = 14;
const H_NEXT: usize = 16;
const H_W: usize = 18;
const H_H: usize = 20;
const H_PITCH: usize = 22;
const H_PX: usize = 24;
const H_KIND: usize = 25;
const H_VALID: usize = 26;
const VALID_BIT: u8 = 1;
const REF_BIT: u8 = 2;
// Cell header: w0 u32 (bit0-20 cp, 21-22 kind-1, 23-24 face, 25 ref, 26 half,
// 31 valid), next u16, set u16. The bitmap follows at CELL_HDR_BYTES.
const C_W0: usize = 0;
const C_NEXT: usize = 4;
const C_SET: usize = 6;
const W0_CP_MASK: u32 = 0x1f_ffff;
const W0_KIND_SHIFT: u32 = 21;
const W0_FACE_SHIFT: u32 = 23;
const W0_REF: u32 = 1 << 25;
const W0_HALF: u32 = 1 << 26;
const W0_VALID: u32 = 1 << 31;
const _: () = assert!(CELL_HDR_BYTES == C_SET + 2);

fn get2(b: &[u8], at: usize) -> Option<[u8; 2]> {
    b.get(at..at + 2)?.try_into().ok()
}
fn get4(b: &[u8], at: usize) -> Option<[u8; 4]> {
    b.get(at..at + 4)?.try_into().ok()
}
fn put(b: &mut [u8], at: usize, v: &[u8]) -> Option<()> {
    b.get_mut(at..at + v.len())?.copy_from_slice(v);
    Some(())
}
fn byte(b: &[u8], at: usize) -> Option<u8> {
    b.get(at).copied()
}

impl SlotHdr {
    fn decode(b: &[u8]) -> Option<Self> {
        Some(Self {
            codepoint: u32::from_le_bytes(get4(b, H_CP)?),
            age: u32::from_le_bytes(get4(b, H_AGE)?),
            left: i16::from_le_bytes(get2(b, H_LEFT)?),
            top: i16::from_le_bytes(get2(b, H_TOP)?),
            set: u16::from_le_bytes(get2(b, H_SET)?),
            face: u16::from_le_bytes(get2(b, H_FACE)?),
            next: u16::from_le_bytes(get2(b, H_NEXT)?),
            width: u16::from_le_bytes(get2(b, H_W)?),
            height: u16::from_le_bytes(get2(b, H_H)?),
            pitch: u16::from_le_bytes(get2(b, H_PITCH)?),
            px: byte(b, H_PX)?,
            kind: byte(b, H_KIND)?,
            valid: byte(b, H_VALID)?,
            pad: [0; 5],
        })
    }
    fn encode(&self, b: &mut [u8]) -> Option<()> {
        let head = b.get_mut(..SLOT_HDR_BYTES)?;
        head.fill(0);
        put(head, H_CP, &self.codepoint.to_le_bytes())?;
        put(head, H_AGE, &self.age.to_le_bytes())?;
        put(head, H_LEFT, &self.left.to_le_bytes())?;
        put(head, H_TOP, &self.top.to_le_bytes())?;
        put(head, H_SET, &self.set.to_le_bytes())?;
        put(head, H_FACE, &self.face.to_le_bytes())?;
        put(head, H_NEXT, &self.next.to_le_bytes())?;
        put(head, H_W, &self.width.to_le_bytes())?;
        put(head, H_H, &self.height.to_le_bytes())?;
        put(head, H_PITCH, &self.pitch.to_le_bytes())?;
        put(head, H_PX, &[self.px, self.kind, self.valid])
    }
}

/// Owner counters (cache design §6 ①②③, b3/b4 design §2-2). Never per glyph logs.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Stats {
    pub hit_bitmap: u32,
    /// Hits on Space/Kcg/Missing entries (no cmap, no read_at).
    pub hit_fallback: u32,
    pub miss: u32,
    pub commit_bitmap: u32,
    pub commit_fallback: u32,
    pub evict: u32,
    /// Complete bitmaps drawn from the scratch (slot too small, class empty, no arena).
    pub uncached: u32,
    /// OpenType rasters started / failed (failure -> Kcg).
    pub raster: u32,
    pub raster_err: u32,
    /// In neither cmap.
    pub cmap_missing: u32,
    /// Entry lease checks (once per face per text()) and their failures.
    pub validate: u32,
    pub stale: u32,
    /// Requests answered KCG because the engine was not Ready/checked.
    pub kcg_only: u32,
    /// Requests refused while a bitmap is borrowed.
    pub busy: u32,
    /// x87 guard BUSY on the shell side (counted by the caller).
    pub guard_busy: u32,
    pub invalidate: u32,
    /// Always 0 since F2 (the clock has no age to wrap); kept for the layout.
    pub age_wrap: u32,
}

/// Hash bucket of a key (public so tests can build collisions).
pub fn bucket_of(key: GlyphKey) -> usize {
    let mixed = key.cp ^ u32::from(key.set).rotate_left(20) ^ (u32::from(key.px) << 12);
    (mixed.wrapping_mul(0x9e37_79b1) >> 16) as usize & (BUCKETS - 1)
}

/// Slot classes (index into the clock state).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ClassId {
    Cell = 0,
    Big16 = 1,
    Big32 = 2,
}
const CLASSES: usize = 3;
const CLASS_ORDER: [ClassId; CLASSES] = [ClassId::Cell, ClassId::Big16, ClassId::Big32];

/// Decoded slot, any class.
#[derive(Clone, Copy)]
struct Meta {
    key: GlyphKey,
    entry: Entry,
    next: u16,
    valid: bool,
    referenced: bool,
}

/// Static context (buckets, clock state, counters) plus a borrowed arena.
/// `new()` and an all-zero value are the same detached, empty cache (buckets
/// hold slot + 1).
pub struct GlyphCache {
    buckets: [u16; BUCKETS],
    arena: Option<&'static mut [u8]>,
    n_cell: u16,
    n16: u16,
    n32: u16,
    n_valid: u16,
    /// Next never-used slot of each class (class-relative).
    fill: [u16; CLASSES],
    /// Clock hand of each class (class-relative).
    hand: [u16; CLASSES],
    /// Slots examined by the clock (host tests bound it; not a Stats field).
    probes: u32,
    stats: Stats,
}
const _: () = assert!(core::mem::size_of::<GlyphCache>() <= CACHE_STATIC_BYTES);

impl Default for GlyphCache {
    fn default() -> Self {
        Self::new()
    }
}

/// Bitmap fits the cell of width `cw` when placed at the face's ascent.
fn fits_cell(e: &Entry, cw: usize) -> bool {
    if e.kind != Kind::Bitmap {
        return true;
    }
    let Some(asc) = ascent16(e.face) else {
        return false;
    };
    let left = i32::from(e.left);
    let y0 = i32::from(asc) - i32::from(e.top);
    left >= 0
        && left + i32::from(e.width) <= cw as i32
        && y0 >= 0
        && y0 + i32::from(e.height) <= CELL_H_PX as i32
}

/// Copy a fitting bitmap into cell coordinates (row y0 = ascent - top, shifted
/// right by `left`; Half uses the first CELL_H_PX bytes).
fn to_cell(e: &Entry, bits: &[u8], cw: usize, out: &mut [u8; CELL_BITMAP_BYTES]) -> Option<()> {
    let asc = ascent16(e.face)?;
    let pitch = usize::from(e.pitch);
    let cell_pitch = cw / 8;
    let y0 = usize::try_from(i32::from(asc) - i32::from(e.top)).ok()?;
    let left = u32::try_from(e.left).ok()?;
    let width = u32::from(e.width).min(16);
    let mask: u16 = if width == 0 { 0 } else { u16::MAX << (16 - width) };
    for r in 0..usize::from(e.height) {
        let row = bits.get(r * pitch..r * pitch + pitch)?;
        let hi = u16::from(row.first().copied().unwrap_or(0));
        let lo = u16::from(row.get(1).copied().unwrap_or(0));
        let v = (((hi << 8) | lo) & mask) >> left;
        let at = (y0 + r) * cell_pitch;
        let dst = out.get_mut(at..at + cell_pitch)?;
        for (k, d) in dst.iter_mut().enumerate() {
            *d = (v >> (8 - 8 * k as u32)) as u8;
        }
    }
    Some(())
}

impl GlyphCache {
    pub const fn new() -> Self {
        Self {
            buckets: [0; BUCKETS],
            arena: None,
            n_cell: 0,
            n16: 0,
            n32: 0,
            n_valid: 0,
            fill: [0; CLASSES],
            hand: [0; CLASSES],
            probes: 0,
            stats: Stats {
                hit_bitmap: 0,
                hit_fallback: 0,
                miss: 0,
                commit_bitmap: 0,
                commit_fallback: 0,
                evict: 0,
                uncached: 0,
                raster: 0,
                raster_err: 0,
                cmap_missing: 0,
                validate: 0,
                stale: 0,
                kcg_only: 0,
                busy: 0,
                guard_busy: 0,
                invalidate: 0,
                age_wrap: 0,
            },
        }
    }

    /// Arena bytes needed for a layout.
    pub const fn arena_bytes(n_cell: usize, n16: usize, n32: usize) -> usize {
        n_cell * CELL_SLOT_BYTES + n16 * SLOT16_BYTES + n32 * SLOT32_BYTES
    }

    /// Take `arena` with `n_cell`/`n16`/`n32` slots (the layout changes only
    /// here, i.e. on an empty cache). Rejected (and handed back) when one is
    /// already attached or it is too small.
    pub fn attach(
        &mut self,
        arena: &'static mut [u8],
        n_cell: usize,
        n16: usize,
        n32: usize,
    ) -> core::result::Result<(), &'static mut [u8]> {
        let slots = n_cell.checked_add(n16).and_then(|n| n.checked_add(n32));
        let need = n_cell
            .checked_mul(CELL_SLOT_BYTES)
            .zip(n16.checked_mul(SLOT16_BYTES))
            .and_then(|(a, b)| a.checked_add(b))
            .zip(n32.checked_mul(SLOT32_BYTES))
            .and_then(|(a, b)| a.checked_add(b));
        let ok = self.arena.is_none()
            && matches!(slots, Some(n) if n < usize::from(NONE))
            && need.is_some_and(|need| need <= arena.len());
        if !ok {
            return Err(arena);
        }
        self.arena = Some(arena);
        self.n_cell = n_cell as u16;
        self.n16 = n16 as u16;
        self.n32 = n32 as u16;
        self.invalidate_all();
        Ok(())
    }

    /// Empty the cache and give the arena back (the app frees it on trim).
    pub fn detach(&mut self) -> Option<&'static mut [u8]> {
        self.invalidate_all();
        self.n_cell = 0;
        self.n16 = 0;
        self.n32 = 0;
        self.arena.take()
    }

    pub fn has_arena(&self) -> bool {
        self.arena.is_some()
    }
    /// (n_cell, n16, n32).
    pub fn layout(&self) -> (usize, usize, usize) {
        (usize::from(self.n_cell), usize::from(self.n16), usize::from(self.n32))
    }
    pub fn n_valid(&self) -> usize {
        usize::from(self.n_valid)
    }
    /// Slots examined by the clock since the cache was made (host tests bound it).
    pub fn clock_probes(&self) -> u32 {
        self.probes
    }
    pub fn stats(&self) -> &Stats {
        &self.stats
    }
    pub fn stats_mut(&mut self) -> &mut Stats {
        &mut self.stats
    }

    /// The one invalidation: every slot invalid and unreferenced, buckets
    /// empty, each class's fill and hand back to slot 0.
    pub fn invalidate_all(&mut self) {
        self.buckets = [0; BUCKETS];
        self.n_valid = 0;
        self.fill = [0; CLASSES];
        self.hand = [0; CLASSES];
        self.stats.invalidate = self.stats.invalidate.wrapping_add(1);
        for slot in 0..self.slots_total() {
            let cell = self.class_of(slot) == ClassId::Cell;
            if let Some(b) = self.slot_mut(slot) {
                if cell {
                    let _ = put(b, C_W0, &0u32.to_le_bytes());
                } else if let Some(v) = b.get_mut(H_VALID) {
                    *v = 0;
                }
            }
        }
    }

    fn slots_total(&self) -> usize {
        usize::from(self.n_cell) + usize::from(self.n16) + usize::from(self.n32)
    }

    /// (first slot, count, slot bytes) of a class.
    fn span(&self, class: ClassId) -> (usize, usize, usize) {
        let n_cell = usize::from(self.n_cell);
        let n16 = usize::from(self.n16);
        match class {
            ClassId::Cell => (0, n_cell, CELL_SLOT_BYTES),
            ClassId::Big16 => (n_cell, n16, SLOT16_BYTES),
            ClassId::Big32 => (n_cell + n16, usize::from(self.n32), SLOT32_BYTES),
        }
    }

    fn class_of(&self, slot: usize) -> ClassId {
        let n_cell = usize::from(self.n_cell);
        if slot < n_cell {
            ClassId::Cell
        } else if slot < n_cell + usize::from(self.n16) {
            ClassId::Big16
        } else {
            ClassId::Big32
        }
    }

    fn slot_range(&self, slot: usize) -> Option<Range<usize>> {
        let mut start = 0;
        for class in CLASS_ORDER {
            let (first, count, bytes) = self.span(class);
            if slot < first + count {
                let at = start + (slot - first) * bytes;
                return Some(at..at + bytes);
            }
            start += count * bytes;
        }
        None
    }

    fn slot(&self, slot: usize) -> Option<&[u8]> {
        let range = self.slot_range(slot)?;
        self.arena.as_deref()?.get(range)
    }

    fn slot_mut(&mut self, slot: usize) -> Option<&mut [u8]> {
        let range = self.slot_range(slot)?;
        self.arena.as_deref_mut()?.get_mut(range)
    }

    fn meta(&self, slot: usize) -> Option<Meta> {
        let b = self.slot(slot)?;
        if self.class_of(slot) == ClassId::Cell {
            let w0 = u32::from_le_bytes(get4(b, C_W0)?);
            let kind = Kind::from_code(((w0 >> W0_KIND_SHIFT) & 3) as u8 + 1)?;
            let face = FaceId::from_code(((w0 >> W0_FACE_SHIFT) & 3) as u16);
            let entry = if kind == Kind::Bitmap {
                let cw = if w0 & W0_HALF != 0 { CELL_HALF_W } else { CELL_FULL_W };
                Entry {
                    kind,
                    face,
                    left: 0,
                    top: ascent16(face).unwrap_or(0),
                    width: cw as u16,
                    height: CELL_H_PX as u16,
                    pitch: (cw / 8) as u16,
                }
            } else {
                Entry {
                    kind,
                    ..Entry::default()
                }
            };
            return Some(Meta {
                key: GlyphKey {
                    set: u16::from_le_bytes(get2(b, C_SET)?),
                    cp: w0 & W0_CP_MASK,
                    px: RASTER_PPEM as u8,
                },
                entry,
                next: u16::from_le_bytes(get2(b, C_NEXT)?),
                valid: w0 & W0_VALID != 0,
                referenced: w0 & W0_REF != 0,
            });
        }
        let h = SlotHdr::decode(b)?;
        Some(Meta {
            key: GlyphKey {
                set: h.set,
                cp: h.codepoint,
                px: h.px,
            },
            entry: Entry {
                kind: Kind::from_code(h.kind).unwrap_or_default(),
                face: FaceId::from_code(h.face),
                left: h.left,
                top: h.top,
                width: h.width,
                height: h.height,
                pitch: h.pitch,
            },
            next: h.next,
            valid: h.valid & VALID_BIT != 0,
            referenced: h.valid & REF_BIT != 0,
        })
    }

    fn set_next(&mut self, slot: usize, next: u16) -> Option<()> {
        let at = if self.class_of(slot) == ClassId::Cell { C_NEXT } else { H_NEXT };
        put(self.slot_mut(slot)?, at, &next.to_le_bytes())
    }

    fn set_ref(&mut self, slot: usize, on: bool) -> Option<()> {
        let cell = self.class_of(slot) == ClassId::Cell;
        let b = self.slot_mut(slot)?;
        if cell {
            let w0 = u32::from_le_bytes(get4(b, C_W0)?);
            let w0 = if on { w0 | W0_REF } else { w0 & !W0_REF };
            put(b, C_W0, &w0.to_le_bytes())
        } else {
            let v = b.get_mut(H_VALID)?;
            *v = if on { *v | REF_BIT } else { *v & !REF_BIT };
            Some(())
        }
    }

    fn head(&self, bucket: usize) -> u16 {
        match self.buckets.get(bucket).copied() {
            Some(0) | None => NONE,
            Some(v) => v - 1,
        }
    }

    fn set_head(&mut self, bucket: usize, slot: u16) {
        if let Some(b) = self.buckets.get_mut(bucket) {
            *b = if slot == NONE { 0 } else { slot + 1 };
        }
    }

    fn find(&self, key: GlyphKey) -> Option<(usize, Meta)> {
        let mut cur = self.head(bucket_of(key));
        // Bounded by the slot count: a corrupt chain can never loop forever.
        for _ in 0..self.slots_total() {
            if cur == NONE {
                return None;
            }
            let m = self.meta(usize::from(cur))?;
            if m.valid && m.key == key {
                return Some((usize::from(cur), m));
            }
            cur = m.next;
        }
        None
    }

    /// Presence only: does not touch reference bits or counters.
    pub fn contains(&self, key: GlyphKey) -> bool {
        self.find(key).is_some()
    }

    /// Hit: set the reference bit and return (slot, entry). Counters are the caller's.
    pub fn lookup(&mut self, key: GlyphKey) -> Option<(u16, Entry)> {
        let (slot, m) = self.find(key)?;
        if !m.referenced {
            self.set_ref(slot, true)?;
        }
        Some((slot as u16, m.entry))
    }

    /// Entry of a valid slot, as a hit returns it (the cell form for a Cell).
    pub fn entry(&self, slot: u16) -> Option<Entry> {
        let m = self.meta(usize::from(slot))?;
        if m.valid {
            Some(m.entry)
        } else {
            None
        }
    }

    /// Bitmap bytes of a valid slot (pitch * height after the header).
    pub fn bits(&self, slot: u16) -> Option<&[u8]> {
        let slot = usize::from(slot);
        let m = self.meta(slot)?;
        if !m.valid {
            return None;
        }
        let at = if self.class_of(slot) == ClassId::Cell {
            CELL_HDR_BYTES
        } else {
            SLOT_HDR_BYTES
        };
        self.slot(slot)?.get(at..at + m.entry.bytes())
    }

    /// The class a complete result goes to, or Limit (the caller draws it uncached).
    fn class_for(&self, key: GlyphKey, e: &Entry, cell: Cell) -> Result<ClassId> {
        if e.kind == Kind::Bitmap && usize::from(e.pitch) != usize::from(e.width).div_ceil(8) {
            return Err(Error::Limit);
        }
        let (class, width, height) = match key.px {
            16 if self.n_cell > 0 && fits_cell(e, cell.width()) => return Ok(ClassId::Cell),
            16 => (ClassId::Big16, SLOT16_W, SLOT16_H),
            32 => (ClassId::Big32, SLOT32_W, SLOT32_H),
            _ => return Err(Error::Limit),
        };
        let (_, count, bytes) = self.span(class);
        if count == 0
            || (e.kind == Kind::Bitmap
                && (usize::from(e.width) > width
                    || usize::from(e.height) > height
                    || e.bytes() > bytes - SLOT_HDR_BYTES))
        {
            return Err(Error::Limit);
        }
        Ok(class)
    }

    /// Victim of a class: the next never-used slot, else the clock from the
    /// hand (an invalid slot or a clear reference bit; a set bit is cleared
    /// and passed). One sweep clears every bit, so it stops within count + 1.
    fn victim(&mut self, class: ClassId) -> Result<usize> {
        let (first, count, _) = self.span(class);
        let i = class as usize;
        if count == 0 {
            return Err(Error::Limit);
        }
        let fill = self.fill.get_mut(i).ok_or(Error::Invalid)?;
        if usize::from(*fill) < count {
            let slot = first + usize::from(*fill);
            *fill += 1;
            return Ok(slot);
        }
        for _ in 0..=count {
            let hand = self.hand.get_mut(i).ok_or(Error::Invalid)?;
            let at = usize::from(*hand) % count;
            *hand = ((at + 1) % count) as u16;
            self.probes = self.probes.wrapping_add(1);
            let slot = first + at;
            let m = self.meta(slot).ok_or(Error::Invalid)?;
            if !m.valid || !m.referenced {
                return Ok(slot);
            }
            self.set_ref(slot, false).ok_or(Error::Invalid)?;
        }
        Err(Error::Invalid)
    }

    /// Store a complete result of a glyph drawn in a `cell`. Limit when the
    /// key's class has no slot or the bitmap does not fit (the caller draws it
    /// uncached); nothing that can fail runs after the victim is chosen. The
    /// stored entry (the cell form for a Cell) is read back with `entry(slot)`.
    pub fn commit(&mut self, key: GlyphKey, e: &Entry, bits: &[u8], cell: Cell) -> Result<u16> {
        if self.arena.is_none() {
            return Err(Error::Limit);
        }
        let class = self.class_for(key, e, cell)?;
        let need = if e.kind == Kind::Bitmap { e.bytes() } else { 0 };
        let src = bits.get(..need).ok_or(Error::Bounds)?;
        let mut packed = [0u8; CELL_BITMAP_BYTES];
        if class == ClassId::Cell && need != 0 {
            to_cell(e, src, cell.width(), &mut packed).ok_or(Error::Bounds)?;
        }
        if self.contains(key) {
            return Err(Error::Invalid);
        }
        let victim = self.victim(class)?;
        let old = self.meta(victim).ok_or(Error::Invalid)?;
        if old.valid {
            self.unlink(victim, old)?;
            self.stats.evict = self.stats.evict.wrapping_add(1);
            self.n_valid = self.n_valid.saturating_sub(1);
        }
        let bucket = bucket_of(key);
        let next = self.head(bucket);
        let b = self.slot_mut(victim).ok_or(Error::Invalid)?;
        if class == ClassId::Cell {
            let face = if e.kind == Kind::Bitmap { e.face as u32 } else { 0 };
            let half = if cell == Cell::Half { W0_HALF } else { 0 };
            let w0 = (key.cp & W0_CP_MASK)
                | ((e.kind as u32 - 1) << W0_KIND_SHIFT)
                | (face << W0_FACE_SHIFT)
                | half
                | W0_VALID;
            put(b, CELL_HDR_BYTES, &packed).ok_or(Error::Invalid)?;
            put(b, C_NEXT, &next.to_le_bytes()).ok_or(Error::Invalid)?;
            put(b, C_SET, &key.set.to_le_bytes()).ok_or(Error::Invalid)?;
            put(b, C_W0, &w0.to_le_bytes()).ok_or(Error::Invalid)?;
        } else {
            let h = SlotHdr {
                codepoint: key.cp,
                age: 0,
                left: e.left,
                top: e.top,
                set: key.set,
                face: e.face as u16,
                next,
                width: if need == 0 { 0 } else { e.width },
                height: if need == 0 { 0 } else { e.height },
                pitch: if need == 0 { 0 } else { e.pitch },
                px: key.px,
                kind: e.kind as u8,
                valid: VALID_BIT,
                pad: [0; 5],
            };
            b.get_mut(SLOT_HDR_BYTES..SLOT_HDR_BYTES + need)
                .ok_or(Error::Invalid)?
                .copy_from_slice(src);
            h.encode(b).ok_or(Error::Invalid)?;
        }
        // Publish last: hash link and count.
        self.set_head(bucket, victim as u16);
        self.n_valid += 1;
        if e.kind == Kind::Bitmap {
            self.stats.commit_bitmap = self.stats.commit_bitmap.wrapping_add(1);
        } else {
            self.stats.commit_fallback = self.stats.commit_fallback.wrapping_add(1);
        }
        Ok(victim as u16)
    }

    /// Remove `slot` from its bucket chain (single-linked: walk from the head).
    fn unlink(&mut self, slot: usize, m: Meta) -> Result<()> {
        let bucket = bucket_of(m.key);
        let target = slot as u16;
        let mut cur = self.head(bucket);
        if cur == target {
            self.set_head(bucket, m.next);
            return Ok(());
        }
        for _ in 0..self.slots_total() {
            if cur == NONE {
                break;
            }
            let ch = self.meta(usize::from(cur)).ok_or(Error::Invalid)?;
            if ch.next == target {
                return self.set_next(usize::from(cur), m.next).ok_or(Error::Invalid);
            }
            cur = ch.next;
        }
        Err(Error::Invalid)
    }
}
