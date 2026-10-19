//! T7b-b3 glyph bitmap cache (glyph cache design §2-§4, b3/b4 design §2-1).
//! One owner-private arena of fixed slots in two pixel classes (16px, 32px).
//! Each slot is a SLOT_HDR_BYTES header followed by the 1bpp bitmap (MSB left,
//! byte-aligned rows, top to bottom). Lookup is by `(set, cp, px)` through a
//! single-linked hash chain of slot numbers; replacement is Plan 9 libdraw style
//! approximate LRU by age within the key's class (invalid slot first, otherwise
//! the smallest age, ties by slot number). Every change of face, setting or age
//! wrap goes through the one `invalidate_all`; there is no per-face invalidation.
//! No heap, no variable-size allocator, no LRU links. The arena belongs to the
//! caller (attach/detach); only complete results are committed.
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

/// The face that produced an entry (drawn_face_slot); None for KCG/Missing.
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

/// Slot header layout (cache design §2). Stored little-endian at the start of
/// every slot; `next` is the hash chain (slot number, NONE = 0xffff).
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
// Header field offsets (same order as SlotHdr).
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
    fn key(&self) -> GlyphKey {
        GlyphKey {
            set: self.set,
            cp: self.codepoint,
            px: self.px,
        }
    }
    fn entry(&self) -> Option<Entry> {
        Some(Entry {
            kind: Kind::from_code(self.kind)?,
            face: FaceId::from_code(self.face),
            left: self.left,
            top: self.top,
            width: self.width,
            height: self.height,
            pitch: self.pitch,
        })
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
    pub age_wrap: u32,
}

/// Hash bucket of a key (public so tests can build collisions).
pub fn bucket_of(key: GlyphKey) -> usize {
    let mixed = key.cp ^ u32::from(key.set).rotate_left(20) ^ (u32::from(key.px) << 12);
    (mixed.wrapping_mul(0x9e37_79b1) >> 16) as usize & (BUCKETS - 1)
}

struct Class {
    first: usize,
    count: usize,
    width: usize,
    height: usize,
    bytes: usize,
}

/// Static context (buckets, counters) plus a borrowed arena. `new()` and an
/// all-zero value are the same detached, empty cache (buckets hold slot + 1).
pub struct GlyphCache {
    buckets: [u16; BUCKETS],
    age: u32,
    arena: Option<&'static mut [u8]>,
    n16: u16,
    n32: u16,
    n_valid: u16,
    stats: Stats,
}
const _: () = assert!(core::mem::size_of::<GlyphCache>() <= CACHE_STATIC_BYTES);

impl Default for GlyphCache {
    fn default() -> Self {
        Self::new()
    }
}

impl GlyphCache {
    pub const fn new() -> Self {
        Self {
            buckets: [0; BUCKETS],
            age: 0,
            arena: None,
            n16: 0,
            n32: 0,
            n_valid: 0,
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
    pub const fn arena_bytes(n16: usize, n32: usize) -> usize {
        n16 * SLOT16_BYTES + n32 * SLOT32_BYTES
    }

    /// Take `arena` with `n16`/`n32` slots (the layout changes only here, i.e.
    /// on an empty cache). Rejected (and handed back) when one is already
    /// attached or it is too small.
    pub fn attach(
        &mut self,
        arena: &'static mut [u8],
        n16: usize,
        n32: usize,
    ) -> core::result::Result<(), &'static mut [u8]> {
        let slots = n16.checked_add(n32);
        let ok = self.arena.is_none()
            && matches!(slots, Some(n) if n < usize::from(NONE))
            && n16
                .checked_mul(SLOT16_BYTES)
                .zip(n32.checked_mul(SLOT32_BYTES))
                .and_then(|(a, b)| a.checked_add(b))
                .is_some_and(|need| need <= arena.len());
        if !ok {
            return Err(arena);
        }
        self.arena = Some(arena);
        self.n16 = n16 as u16;
        self.n32 = n32 as u16;
        self.invalidate_all();
        Ok(())
    }

    /// Empty the cache and give the arena back (the app frees it on trim).
    pub fn detach(&mut self) -> Option<&'static mut [u8]> {
        self.invalidate_all();
        self.n16 = 0;
        self.n32 = 0;
        self.arena.take()
    }

    pub fn has_arena(&self) -> bool {
        self.arena.is_some()
    }
    pub fn layout(&self) -> (usize, usize) {
        (usize::from(self.n16), usize::from(self.n32))
    }
    pub fn n_valid(&self) -> usize {
        usize::from(self.n_valid)
    }
    pub fn age(&self) -> u32 {
        self.age
    }
    pub fn stats(&self) -> &Stats {
        &self.stats
    }
    pub fn stats_mut(&mut self) -> &mut Stats {
        &mut self.stats
    }
    /// Host tests only: move the age counter next to its wrap.
    #[doc(hidden)]
    pub fn set_age_for_test(&mut self, age: u32) {
        self.age = age;
    }

    /// The one invalidation: every slot invalid, buckets and age reset.
    pub fn invalidate_all(&mut self) {
        self.buckets = [0; BUCKETS];
        self.age = 0;
        self.n_valid = 0;
        self.stats.invalidate = self.stats.invalidate.wrapping_add(1);
        let total = usize::from(self.n16) + usize::from(self.n32);
        for slot in 0..total {
            if let Some(range) = self.slot_range(slot) {
                if let Some(b) = self.arena.as_deref_mut().and_then(|a| a.get_mut(range)) {
                    if let Some(v) = b.get_mut(H_VALID) {
                        *v = 0;
                    }
                }
            }
        }
    }

    fn class(&self, px: u8) -> Option<Class> {
        let n16 = usize::from(self.n16);
        match px {
            16 => Some(Class {
                first: 0,
                count: n16,
                width: SLOT16_W,
                height: SLOT16_H,
                bytes: SLOT16_BYTES,
            }),
            32 => Some(Class {
                first: n16,
                count: usize::from(self.n32),
                width: SLOT32_W,
                height: SLOT32_H,
                bytes: SLOT32_BYTES,
            }),
            _ => None,
        }
    }

    fn slot_range(&self, slot: usize) -> Option<Range<usize>> {
        let n16 = usize::from(self.n16);
        let (start, len) = if slot < n16 {
            (slot * SLOT16_BYTES, SLOT16_BYTES)
        } else if slot < n16 + usize::from(self.n32) {
            (n16 * SLOT16_BYTES + (slot - n16) * SLOT32_BYTES, SLOT32_BYTES)
        } else {
            return None;
        };
        Some(start..start + len)
    }

    fn slot(&self, slot: usize) -> Option<&[u8]> {
        let range = self.slot_range(slot)?;
        self.arena.as_deref()?.get(range)
    }

    fn slot_mut(&mut self, slot: usize) -> Option<&mut [u8]> {
        let range = self.slot_range(slot)?;
        self.arena.as_deref_mut()?.get_mut(range)
    }

    fn hdr(&self, slot: usize) -> Option<SlotHdr> {
        SlotHdr::decode(self.slot(slot)?)
    }

    fn set_hdr(&mut self, slot: usize, h: &SlotHdr) -> Option<()> {
        h.encode(self.slot_mut(slot)?)
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

    fn slots_total(&self) -> usize {
        usize::from(self.n16) + usize::from(self.n32)
    }

    fn find(&self, key: GlyphKey) -> Option<(usize, SlotHdr)> {
        let mut cur = self.head(bucket_of(key));
        // Bounded by the slot count: a corrupt chain can never loop forever.
        for _ in 0..self.slots_total() {
            if cur == NONE {
                return None;
            }
            let h = self.hdr(usize::from(cur))?;
            if h.valid != 0 && h.key() == key {
                return Some((usize::from(cur), h));
            }
            cur = h.next;
        }
        None
    }

    /// Presence only: does not touch ages or counters.
    pub fn contains(&self, key: GlyphKey) -> bool {
        self.find(key).is_some()
    }

    /// Advance the owner's age; on wrap everything is invalidated first and
    /// false is returned (a hit candidate is then a miss).
    fn tick(&mut self) -> bool {
        if self.age == u32::MAX {
            self.stats.age_wrap = self.stats.age_wrap.wrapping_add(1);
            self.invalidate_all();
            self.age = 1;
            return false;
        }
        self.age += 1;
        true
    }

    /// Hit: refresh the age and return (slot, entry). Counters are the caller's.
    pub fn lookup(&mut self, key: GlyphKey) -> Option<(u16, Entry)> {
        let (slot, mut h) = self.find(key)?;
        let entry = h.entry()?;
        if !self.tick() {
            return None;
        }
        h.age = self.age;
        self.set_hdr(slot, &h)?;
        Some((slot as u16, entry))
    }

    /// Bitmap bytes of a valid slot (pitch * height after the header).
    pub fn bits(&self, slot: u16) -> Option<&[u8]> {
        let b = self.slot(usize::from(slot))?;
        let h = SlotHdr::decode(b)?;
        if h.valid == 0 {
            return None;
        }
        let len = usize::from(h.pitch) * usize::from(h.height);
        b.get(SLOT_HDR_BYTES..SLOT_HDR_BYTES + len)
    }

    /// Store a complete result. Limit when the key's class has no slot or the
    /// bitmap does not fit its slot (the caller draws it uncached); nothing
    /// that can fail runs after the victim is chosen.
    pub fn commit(&mut self, key: GlyphKey, e: &Entry, bits: &[u8]) -> Result<u16> {
        let class = self.class(key.px).ok_or(Error::Limit)?;
        if class.count == 0 || self.arena.is_none() {
            return Err(Error::Limit);
        }
        let need = if e.kind == Kind::Bitmap { e.bytes() } else { 0 };
        if e.kind == Kind::Bitmap
            && (usize::from(e.width) > class.width
                || usize::from(e.height) > class.height
                || usize::from(e.pitch) != usize::from(e.width).div_ceil(8)
                || need > class.bytes - SLOT_HDR_BYTES)
        {
            return Err(Error::Limit);
        }
        let src = bits.get(..need).ok_or(Error::Bounds)?;
        if self.contains(key) {
            return Err(Error::Invalid);
        }
        self.tick();
        // Victim: first invalid slot, else the smallest age (ties: lower slot).
        let mut victim = class.first;
        let mut best: Option<u32> = None;
        for slot in class.first..class.first + class.count {
            let h = self.hdr(slot).ok_or(Error::Invalid)?;
            if h.valid == 0 {
                victim = slot;
                break;
            }
            if best.is_none_or(|age| h.age < age) {
                best = Some(h.age);
                victim = slot;
            }
        }
        let old = self.hdr(victim).ok_or(Error::Invalid)?;
        if old.valid != 0 {
            self.unlink(victim, old)?;
            self.stats.evict = self.stats.evict.wrapping_add(1);
            self.n_valid = self.n_valid.saturating_sub(1);
        }
        let bucket = bucket_of(key);
        let h = SlotHdr {
            codepoint: key.cp,
            age: self.age,
            left: e.left,
            top: e.top,
            set: key.set,
            face: e.face as u16,
            next: self.head(bucket),
            width: if need == 0 { 0 } else { e.width },
            height: if need == 0 { 0 } else { e.height },
            pitch: if need == 0 { 0 } else { e.pitch },
            px: key.px,
            kind: e.kind as u8,
            valid: 1,
            pad: [0; 5],
        };
        let b = self.slot_mut(victim).ok_or(Error::Invalid)?;
        b.get_mut(SLOT_HDR_BYTES..SLOT_HDR_BYTES + need)
            .ok_or(Error::Invalid)?
            .copy_from_slice(src);
        h.encode(b).ok_or(Error::Invalid)?;
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
    fn unlink(&mut self, slot: usize, h: SlotHdr) -> Result<()> {
        let bucket = bucket_of(h.key());
        let target = slot as u16;
        let mut cur = self.head(bucket);
        if cur == target {
            self.set_head(bucket, h.next);
            return Ok(());
        }
        for _ in 0..self.slots_total() {
            if cur == NONE {
                break;
            }
            let mut ch = self.hdr(usize::from(cur)).ok_or(Error::Invalid)?;
            if ch.next == target {
                ch.next = h.next;
                return self.set_hdr(usize::from(cur), &ch).ok_or(Error::Invalid);
            }
            cur = ch.next;
        }
        Err(Error::Invalid)
    }
}
