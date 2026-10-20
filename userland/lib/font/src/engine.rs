//! T7b-b3 font engine (b3/b4 design §2-1..§2-4): the owner's (latin, ja) face
//! pair, one raster scratch and the glyph cache. No heap; the caller owns the
//! storage (gshell .bss, libos32gui exec_heap) and the arena. An all-zero Engine
//! equals `Engine::new()` (closed, detached).
//!
//! Per text(): `begin()` (one lease check per open face) -> `resolve`/`peek`
//! per glyph -> `bitmap()` while drawing -> `release()` -> ... -> `end()`.
//! A returned Bitmap is borrowed until `release`; meanwhile close/trim/
//! invalidate_all/detach refuse with Busy and further resolves answer Kcg.
//! `open` and a resolve that misses read and rasterise (f32): call them inside
//! the x87 guard/fp scope on OS32. Hits and `peek` do not read or use x87.
use crate::{
    cache::{Entry, FaceId, GlyphCache, GlyphKey, Kind, Stats},
    limits::*,
    raster::{PixelSize, RasterScratch},
    reader::{Error, Face, Source},
};

/// Cell width of the glyph, decided by the caller's width rules; it only
/// picks the cmap search order (Half ASCII: latin -> ja, Half above ASCII:
/// latin only, Full: ja only).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Cell {
    Half,
    Full,
}

/// The half-width non-ASCII range (T7b-b5): Latin-1 Supplement and Latin
/// Extended-A/B, `LATIN_HALF_FIRST..=LATIN_HALF_LAST`. Callers use it for the
/// width rule (8px) and resolve these code points with `Cell::Half`.
pub const fn is_latin_half(cp: u32) -> bool {
    cp >= LATIN_HALF_FIRST && cp <= LATIN_HALF_LAST
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Reason {
    NoFpu,
    OpenFailed,
    Nospc,
    Stale,
    Busy,
    /// Shell service table older than version 2 (no font_guard_run).
    OldShell,
    /// The setting selects KCG only (FONT_SET_KCG_ONLY).
    Config,
}

/// Closed is variant 0 so an all-zero Engine is closed.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum State {
    Closed,
    Ready,
    KcgOnly(Reason),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EngineError {
    /// A bitmap is borrowed; nothing was changed.
    Busy,
    Font(Error),
}

/// Result of one glyph. Kcg/Missing/Miss are drawn by the caller's KCG path.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Resolved {
    Bitmap,
    Space,
    Kcg,
    Missing,
    /// `peek` only: not cached, a resolve would rasterise.
    Miss,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
enum At {
    #[default]
    Nowhere,
    Slot(u16),
    Scratch,
}

/// One resolved glyph: placement relative to the pen on the baseline and the
/// face's ascent in rows (draw from `cell.y + ascent - top`).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Glyph {
    pub entry: Entry,
    pub ascent: i16,
    /// The bitmap is in a cache slot (false: the scratch, valid until release).
    pub cached: bool,
    at: At,
}

pub struct Engine<S> {
    latin: Face<S>,
    ja: Face<S>,
    scratch: RasterScratch,
    cache: GlyphCache,
    tmp: [u8; MAX_BITMAP_BYTES],
    set_slot: u16,
    state: State,
    busy: bool,
    checked: bool,
}

impl<S> Default for Engine<S> {
    fn default() -> Self {
        Self::new()
    }
}

const fn px_code(px: PixelSize) -> u8 {
    match px {
        PixelSize::Px16 => RASTER_PPEM as u8,
        PixelSize::Px32 => (RASTER_PPEM as usize * MAX_PIXEL_FACTOR) as u8,
    }
}

fn ascent_of(face: FaceId, px: PixelSize) -> i16 {
    let rows = match face {
        FaceId::Latin => ASCENT_LATIN_PX,
        FaceId::Ja => ASCENT_JA_PX,
        FaceId::None => 0,
    };
    rows * px.factor() as i16
}

impl<S> Engine<S> {
    pub const fn new() -> Self {
        Self {
            latin: Face::new(),
            ja: Face::new(),
            scratch: RasterScratch::new(),
            cache: GlyphCache::new(),
            tmp: [0; MAX_BITMAP_BYTES],
            set_slot: 0,
            state: State::Closed,
            busy: false,
            checked: false,
        }
    }
    pub fn state(&self) -> State {
        self.state
    }
    /// font_set_slot of the current setting generation (advanced by every open).
    pub fn set_slot(&self) -> u16 {
        self.set_slot
    }
    pub fn is_busy(&self) -> bool {
        self.busy
    }
    pub fn cache(&self) -> &GlyphCache {
        &self.cache
    }
    pub fn stats(&self) -> &Stats {
        self.cache.stats()
    }
    /// The shell's x87 guard answered BUSY for this text() (drawn KCG).
    pub fn note_guard_busy(&mut self) {
        let s = self.cache.stats_mut();
        s.guard_busy = s.guard_busy.wrapping_add(1);
    }

    fn not_busy(&self) -> Result<(), EngineError> {
        if self.busy {
            Err(EngineError::Busy)
        } else {
            Ok(())
        }
    }

    fn close_faces(&mut self) {
        self.latin.close();
        self.ja.close();
        self.cache.invalidate_all();
        self.checked = false;
    }

    /// Give the cache an arena (only when none is attached and not busy).
    pub fn attach_arena(
        &mut self,
        arena: &'static mut [u8],
        n16: usize,
        n32: usize,
    ) -> Result<(), &'static mut [u8]> {
        if self.busy {
            return Err(arena);
        }
        self.cache.attach(arena, n16, n32)
    }

    /// Empty the cache and return its arena (the app frees it on trim).
    pub fn detach_arena(&mut self) -> Result<Option<&'static mut [u8]>, EngineError> {
        self.not_busy()?;
        Ok(self.cache.detach())
    }

    /// Close both faces (dropping their sources) and invalidate every entry.
    pub fn close(&mut self) -> Result<(), EngineError> {
        self.not_busy()?;
        self.close_faces();
        self.state = State::Closed;
        Ok(())
    }

    /// Closed faces, empty cache, every request answered KCG for `reason`.
    pub fn set_kcg_only(&mut self, reason: Reason) -> Result<(), EngineError> {
        self.not_busy()?;
        self.close_faces();
        self.state = State::KcgOnly(reason);
        Ok(())
    }

    /// invalidate_all + every face's read cache; faces, sources and arena stay.
    pub fn trim(&mut self) -> Result<(), EngineError> {
        self.not_busy()?;
        self.cache.invalidate_all();
        self.latin.trim();
        self.ja.trim();
        Ok(())
    }

    pub fn invalidate_all(&mut self) -> Result<(), EngineError> {
        self.not_busy()?;
        self.cache.invalidate_all();
        Ok(())
    }

    /// End of one text(): drop the entry check and any borrow.
    pub fn end(&mut self) {
        self.checked = false;
        self.busy = false;
    }

    /// End of drawing one glyph's bitmap.
    pub fn release(&mut self) {
        self.busy = false;
    }

    /// The borrowed bitmap of the last Bitmap result (empty when not borrowed).
    pub fn bitmap(&self, g: &Glyph) -> &[u8] {
        if !self.busy {
            return &[];
        }
        match g.at {
            At::Slot(slot) => self.cache.bits(slot).unwrap_or(&[]),
            At::Scratch => self.tmp.get(..g.entry.bytes()).unwrap_or(&[]),
            At::Nowhere => &[],
        }
    }

    fn usable(&self) -> bool {
        self.state == State::Ready && self.checked
    }

    /// Cached-only presence for the glyphs of one text(): true when some
    /// resolve would rasterise (the shell then enters its x87 guard once).
    pub fn need_raster<I: IntoIterator<Item = u32>>(&self, px: PixelSize, cps: I) -> bool {
        if !self.usable() {
            return false;
        }
        cps.into_iter().any(|cp| {
            !self.cache.contains(GlyphKey {
                set: self.set_slot,
                cp,
                px: px_code(px),
            })
        })
    }

    fn refuse(&mut self, out: &mut Glyph) -> Option<Resolved> {
        *out = Glyph::default();
        if self.busy {
            let s = self.cache.stats_mut();
            s.busy = s.busy.wrapping_add(1);
            return Some(Resolved::Kcg);
        }
        if !self.usable() {
            let s = self.cache.stats_mut();
            s.kcg_only = s.kcg_only.wrapping_add(1);
            return Some(Resolved::Kcg);
        }
        None
    }

    /// Cache lookup only (no cmap, read_at or raster); Miss when absent.
    pub fn peek(&mut self, cp: u32, px: PixelSize, out: &mut Glyph) -> Resolved {
        if let Some(r) = self.refuse(out) {
            return r;
        }
        let key = GlyphKey {
            set: self.set_slot,
            cp,
            px: px_code(px),
        };
        match self.cache.lookup(key) {
            Some((slot, entry)) => self.hit(slot, entry, px, out),
            None => Resolved::Miss,
        }
    }

    fn hit(&mut self, slot: u16, entry: Entry, px: PixelSize, out: &mut Glyph) -> Resolved {
        *out = Glyph {
            entry,
            ascent: ascent_of(entry.face, px),
            cached: true,
            at: At::Slot(slot),
        };
        let s = self.cache.stats_mut();
        match entry.kind {
            Kind::Bitmap => {
                s.hit_bitmap = s.hit_bitmap.wrapping_add(1);
                self.busy = true;
                Resolved::Bitmap
            }
            other => {
                s.hit_fallback = s.hit_fallback.wrapping_add(1);
                out.at = At::Nowhere;
                kind_result(other)
            }
        }
    }

    /// Commit a bitmap-less result (fits any slot; no slot -> stays uncached).
    fn settle(&mut self, key: GlyphKey, entry: Entry, px: PixelSize, out: &mut Glyph) -> Resolved {
        let cached = self.cache.commit(key, &entry, &[]).is_ok();
        *out = Glyph {
            entry,
            ascent: ascent_of(entry.face, px),
            cached,
            at: At::Nowhere,
        };
        kind_result(entry.kind)
    }
}

fn kind_result(kind: Kind) -> Resolved {
    match kind {
        Kind::Bitmap => Resolved::Bitmap,
        Kind::Space => Resolved::Space,
        Kind::Kcg => Resolved::Kcg,
        Kind::Missing => Resolved::Missing,
    }
}

impl<S: Source> Engine<S> {
    /// Open the set in place (call at a safe point, inside the x87 scope):
    /// close the old faces -> invalidate_all -> set_slot += 1 -> open both.
    /// Any failure leaves both closed and the state KcgOnly(OpenFailed).
    pub fn open(&mut self, latin: S, ja: S) -> Result<(), EngineError> {
        self.not_busy()?;
        self.close_faces();
        self.set_slot = self.set_slot.wrapping_add(1);
        let opened = match self.latin.open(latin) {
            Ok(()) => self.ja.open(ja),
            Err(e) => Err(e),
        };
        match opened {
            Ok(()) => {
                self.state = State::Ready;
                Ok(())
            }
            Err(e) => {
                self.close_faces();
                self.state = State::KcgOnly(Reason::OpenFailed);
                Err(EngineError::Font(e))
            }
        }
    }

    /// Entry of one text(): check each open face's lease once. A failure
    /// closes the set, invalidates everything and makes it KcgOnly(Stale).
    /// Returns whether this text() may use the faces.
    pub fn begin(&mut self) -> bool {
        self.checked = false;
        if self.busy || self.state != State::Ready {
            return false;
        }
        let latin = self.latin.validate();
        let ja = self.ja.validate();
        let s = self.cache.stats_mut();
        s.validate = s.validate.wrapping_add(2);
        if latin.is_err() || ja.is_err() {
            s.stale = s.stale.wrapping_add(1);
            self.close_faces();
            self.state = State::KcgOnly(Reason::Stale);
            return false;
        }
        self.checked = true;
        true
    }

    /// One glyph: cache hit, else cmap search (Half latin->ja, Full ja->latin),
    /// complete raster in the scratch, commit, and borrow. Missing/raster
    /// failures commit their fallback kind; a bitmap that does not fit its
    /// slot (or with no arena) is drawn uncached from the scratch.
    pub fn resolve(&mut self, cp: u32, cell: Cell, px: PixelSize, out: &mut Glyph) -> Resolved {
        if let Some(r) = self.refuse(out) {
            return r;
        }
        let key = GlyphKey {
            set: self.set_slot,
            cp,
            px: px_code(px),
        };
        if let Some((slot, entry)) = self.cache.lookup(key) {
            return self.hit(slot, entry, px, out);
        }
        let s = self.cache.stats_mut();
        s.miss = s.miss.wrapping_add(1);
        // Full cells never fall back to the latin face (box/block glyphs overflow
        // the 16px slot); a miss there goes to KCG. Half cells above ASCII never
        // fall back to the ja face either (T7b-b5): its Latin-1 glyphs are
        // full-width and would be cut in half by the 8px cell.
        let order: &[FaceId] = match cell {
            Cell::Half if cp < 0x80 => &[FaceId::Latin, FaceId::Ja],
            Cell::Half => &[FaceId::Latin],
            Cell::Full => &[FaceId::Ja],
        };
        let mut found = None;
        let mut failed = false;
        for &id in order {
            let face = if id == FaceId::Latin { &mut self.latin } else { &mut self.ja };
            match face.glyph_index(cp) {
                Ok(Some(gid)) => {
                    found = Some((id, gid));
                    break;
                }
                Ok(None) => {}
                Err(_) => {
                    failed = true;
                    break;
                }
            }
        }
        let fallback = |kind| Entry {
            kind,
            ..Entry::default()
        };
        let (id, gid) = match found {
            Some(f) => f,
            None if failed => return self.settle(key, fallback(Kind::Kcg), px, out),
            None => {
                let s = self.cache.stats_mut();
                s.cmap_missing = s.cmap_missing.wrapping_add(1);
                return self.settle(key, fallback(Kind::Missing), px, out);
            }
        };
        let s = self.cache.stats_mut();
        s.raster = s.raster.wrapping_add(1);
        let face = if id == FaceId::Latin { &mut self.latin } else { &mut self.ja };
        let placement = match self.scratch.render(face, gid, px, &mut self.tmp) {
            Ok(p) => p,
            Err(_) => {
                let s = self.cache.stats_mut();
                s.raster_err = s.raster_err.wrapping_add(1);
                return self.settle(key, fallback(Kind::Kcg), px, out);
            }
        };
        let mut entry = Entry {
            kind: Kind::Bitmap,
            face: id,
            left: placement.left,
            top: placement.top,
            width: placement.width,
            height: placement.height,
            pitch: placement.pitch,
        };
        if placement.width == 0 || placement.height == 0 {
            entry.kind = Kind::Space;
            entry.width = 0;
            entry.height = 0;
            entry.pitch = 0;
            return self.settle(key, entry, px, out);
        }
        let bits = self.tmp.get(..entry.bytes()).unwrap_or(&[]);
        let (cached, at) = match self.cache.commit(key, &entry, bits) {
            Ok(slot) => (true, At::Slot(slot)),
            Err(_) => {
                let s = self.cache.stats_mut();
                s.uncached = s.uncached.wrapping_add(1);
                (false, At::Scratch)
            }
        };
        *out = Glyph {
            entry,
            ascent: ascent_of(id, px),
            cached,
            at,
        };
        self.busy = true;
        Resolved::Bitmap
    }
}
