//! T7b-b3 engine (b3/b4 design §2-1..§2-4, §5): resolution order, fallback
//! entries, complete-bitmap commit under injected faults, borrowing (busy),
//! entry lease check, reopen order, zero state, and the per-face baseline
//! (ascent) measured on the real Inconsolata / JF Dot Shinonome 16.
use os32_font::{
    cache::{FaceId, Kind},
    engine::{Cell, Engine, EngineError, Glyph, Reason, Resolved, State},
    limits::*,
    raster::PixelSize::{Px16, Px32},
    reader::{Error, Result, Source},
};
use std::{
    cell::{Cell as StdCell, RefCell},
    collections::BTreeMap,
    fs::File,
    os::unix::fs::FileExt,
    path::PathBuf,
    rc::Rc,
};

#[derive(Default)]
struct Probe {
    reads: StdCell<usize>,
    /// Fail (Err) / shorten the read with this 1-based number.
    fail_at: StdCell<Option<usize>>,
    short_at: StdCell<Option<usize>>,
    stale: StdCell<bool>,
    validates: StdCell<usize>,
    log: RefCell<Vec<String>>,
}
struct Memory {
    name: &'static str,
    data: Vec<u8>,
    probe: Rc<Probe>,
}
impl Source for Memory {
    fn file_size(&self) -> u64 {
        self.data.len() as u64
    }
    fn read_at(&mut self, at: u64, dst: &mut [u8]) -> Result<usize> {
        let p = &self.probe;
        p.reads.set(p.reads.get() + 1);
        if p.log.borrow().last().map(|s| s.as_str()) != Some(self.name) {
            p.log.borrow_mut().push(self.name.to_string());
        }
        if p.fail_at.get() == Some(p.reads.get()) {
            return Err(Error::Io);
        }
        let n = if p.short_at.get() == Some(p.reads.get()) { dst.len() / 2 } else { dst.len() };
        dst[..n].copy_from_slice(&self.data[at as usize..at as usize + n]);
        Ok(n)
    }
    fn validate(&mut self) -> Result<()> {
        self.probe.validates.set(self.probe.validates.get() + 1);
        if self.probe.stale.get() {
            Err(Error::Io)
        } else {
            Ok(())
        }
    }
}
impl Drop for Memory {
    fn drop(&mut self) {
        self.probe.log.borrow_mut().push(format!("close {}", self.name));
    }
}

fn w16(b: &mut [u8], at: usize, v: u16) {
    b[at..at + 2].copy_from_slice(&v.to_be_bytes());
}
fn w32(b: &mut [u8], at: usize, v: u32) {
    b[at..at + 4].copy_from_slice(&v.to_be_bytes());
}
fn word(b: &mut Vec<u8>, v: u16) {
    b.extend(v.to_be_bytes());
}
const UPEM: u16 = 1024; // 64 units per pixel at 16ppem

/// Synthetic SFNT: cmap format 12 groups (cp -> gid), glyphs in order (gid 0 first).
fn fixture(map: &[(u32, u16)], glyphs: Vec<Vec<u8>>) -> Vec<u8> {
    let n = glyphs.len() as u16;
    let mut head = vec![0; 54];
    w32(&mut head, 0, 0x10000);
    w32(&mut head, 12, 0x5f0f3cf5);
    w16(&mut head, 18, UPEM);
    w16(&mut head, 50, 1);
    let mut hhea = vec![0; 36];
    w32(&mut hhea, 0, 0x10000);
    w16(&mut hhea, 4, 880);
    w16(&mut hhea, 6, (-144i16) as u16);
    w16(&mut hhea, 34, 1);
    let mut maxp = vec![0; 32];
    w32(&mut maxp, 0, 0x10000);
    w16(&mut maxp, 4, n);
    let mut cmap = vec![0; 28 + map.len() * 12];
    w16(&mut cmap, 2, 1);
    w16(&mut cmap, 4, 3);
    w16(&mut cmap, 6, 10);
    w32(&mut cmap, 8, 12);
    w16(&mut cmap, 12, 12);
    w32(&mut cmap, 16, 16 + map.len() as u32 * 12);
    w32(&mut cmap, 24, map.len() as u32);
    for (i, (cp, gid)) in map.iter().enumerate() {
        w32(&mut cmap, 28 + i * 12, *cp);
        w32(&mut cmap, 32 + i * 12, *cp);
        w32(&mut cmap, 36 + i * 12, u32::from(*gid));
    }
    let mut glyf = Vec::new();
    let mut loca = Vec::new();
    for g in glyphs {
        loca.extend((glyf.len() as u32).to_be_bytes());
        glyf.extend(g);
        while glyf.len() % 4 != 0 {
            glyf.push(0);
        }
    }
    loca.extend((glyf.len() as u32).to_be_bytes());
    let mut hmtx = vec![0; 4 + (n as usize - 1) * 2];
    w16(&mut hmtx, 0, UPEM);
    let mut parts = BTreeMap::new();
    for (t, d) in [
        (*b"head", head),
        (*b"hhea", hhea),
        (*b"maxp", maxp),
        (*b"cmap", cmap),
        (*b"glyf", glyf),
        (*b"loca", loca),
        (*b"hmtx", hmtx),
    ] {
        parts.insert(t, d);
    }
    let mut b = vec![0; 12 + parts.len() * 16];
    w32(&mut b, 0, 0x10000);
    w16(&mut b, 4, parts.len() as u16);
    for (i, (t, d)) in parts.into_iter().enumerate() {
        let align = if t == *b"glyf" { 4096 } else { 4 };
        while b.len() % align != 0 {
            b.push(0);
        }
        let off = b.len() as u32;
        b[12 + i * 16..16 + i * 16].copy_from_slice(&t);
        w32(&mut b, 20 + i * 16, off);
        w32(&mut b, 24 + i * 16, d.len() as u32);
        b.extend(d);
    }
    b
}
/// Simple glyph, one rectangle in pixels (x0, y0)-(x1, y1), y up from the baseline.
fn rect(x0: i32, y0: i32, x1: i32, y1: i32) -> Vec<u8> {
    let pts = [(x0, y0), (x0, y1), (x1, y1), (x1, y0)].map(|(x, y)| (x * 64, y * 64));
    let mut b = vec![0; 10];
    w16(&mut b, 0, 1);
    word(&mut b, 3);
    word(&mut b, 0);
    b.extend([1u8; 4]);
    let (mut x, mut y) = (0, 0);
    let mut ys = Vec::new();
    for (px, py) in pts {
        word(&mut b, (px - x) as i16 as u16);
        ys.push((py - y) as i16 as u16);
        (x, y) = (px, py);
    }
    for v in ys {
        word(&mut b, v);
    }
    b
}

const SPACE: u32 = 0x20;
const WIDE: u32 = 'W' as u32; // 25px wide: does not fit a 20x21 slot
const HUGE: u32 = 'X' as u32; // 41px: above MAX_BBOX_PX -> raster Limit
const KANA: u32 = 0x3042;
const KANJI: u32 = 0x4e9c;
const NOWHERE: u32 = 0x2603;

fn latin_font() -> Vec<u8> {
    fixture(
        &[(SPACE, 3), ('A' as u32, 1), ('B' as u32, 2), (WIDE, 4), (HUGE, 5)],
        vec![rect(0, 0, 1, 1), rect(1, 0, 7, 10), rect(0, -3, 6, 12), Vec::new(), rect(0, 0, 25, 8), rect(0, 0, 41, 8)],
    )
}
fn ja_font() -> Vec<u8> {
    fixture(
        &[('A' as u32, 1), (KANA, 2), (KANJI, 3)],
        vec![rect(0, 0, 1, 1), rect(0, 0, 16, 14), rect(1, -2, 15, 14), rect(2, 0, 14, 12)],
    )
}

struct Rig {
    engine: Box<Engine<Memory>>,
    latin: Rc<Probe>,
    ja: Rc<Probe>,
}
fn src(name: &'static str, data: Vec<u8>, probe: &Rc<Probe>) -> Memory {
    Memory { name, data, probe: probe.clone() }
}
fn arena(bytes: usize) -> &'static mut [u8] {
    Box::leak(vec![0u8; bytes].into_boxed_slice())
}
fn rig(n16: usize) -> Rig {
    let (latin, ja) = (Rc::new(Probe::default()), Rc::new(Probe::default()));
    let mut engine = Box::new(Engine::new());
    if n16 != 0 {
        engine.attach_arena(arena(n16 * SLOT16_BYTES), n16, 0).unwrap();
    }
    engine.open(src("latin", latin_font(), &latin), src("ja", ja_font(), &ja)).unwrap();
    assert_eq!(engine.state(), State::Ready);
    assert!(engine.begin());
    Rig { engine, latin, ja }
}
fn reads(r: &Rig) -> usize {
    r.latin.reads.get() + r.ja.reads.get()
}
/// Resolve, copy the bitmap, release.
fn draw(e: &mut Engine<Memory>, cp: u32, cell: Cell) -> (Resolved, Glyph, Vec<u8>) {
    let mut g = Glyph::default();
    let r = e.resolve(cp, cell, Px16, &mut g);
    let bits = e.bitmap(&g).to_vec();
    e.release();
    (r, g, bits)
}

#[test]
fn search_order_half_latin_full_ja() {
    let mut r = rig(8);
    let (res, g, bits) = draw(&mut r.engine, 'A' as u32, Cell::Half);
    assert_eq!((res, g.entry.face, g.ascent, g.cached), (Resolved::Bitmap, FaceId::Latin, ASCENT_LATIN_PX, true));
    assert_eq!((g.entry.left, g.entry.top, g.entry.width, g.entry.height), (1, 10, 6, 10));
    assert_eq!(bits.len(), 10);
    assert!(bits.iter().all(|&b| b == 0xfc));
    let (_, g, _) = draw(&mut r.engine, KANA, Cell::Half);
    assert_eq!(g.entry.face, FaceId::Ja, "half: latin first, then ja");
    r.engine.invalidate_all().unwrap();
    let (_, g, _) = draw(&mut r.engine, 'A' as u32, Cell::Full);
    assert_eq!((g.entry.face, g.ascent), (FaceId::Ja, ASCENT_JA_PX), "full: ja first");
    let (_, g, _) = draw(&mut r.engine, 'B' as u32, Cell::Full);
    assert_eq!(g.entry.face, FaceId::Latin, "full: ja, then latin");
    // 32px class: no slot in the standard layout -> complete, uncached.
    let mut g = Glyph::default();
    assert_eq!(r.engine.resolve(KANJI, Cell::Full, Px32, &mut g), Resolved::Bitmap);
    assert_eq!((g.cached, g.ascent, g.entry.width, g.entry.height), (false, 2 * ASCENT_JA_PX, 24, 24));
    assert_eq!(r.engine.bitmap(&g).len(), 3 * 24);
    r.engine.release();
}

#[test]
fn hits_read_nothing_and_match_cold_and_trimmed() {
    let mut r = rig(8);
    let cold: Vec<_> = [KANJI, 'B' as u32].iter().map(|&cp| draw(&mut r.engine, cp, Cell::Full)).collect();
    let before = reads(&r);
    let raster = r.engine.stats().raster;
    for _ in 0..3 {
        let warm: Vec<_> = [KANJI, 'B' as u32].iter().map(|&cp| draw(&mut r.engine, cp, Cell::Full)).collect();
        assert_eq!(warm, cold, "warm hit == cold miss");
    }
    assert_eq!(reads(&r), before, "hit: no cmap/read_at");
    assert_eq!(r.engine.stats().raster, raster);
    assert_eq!(r.engine.stats().hit_bitmap, 6);
    r.engine.trim().unwrap();
    assert_eq!(r.engine.cache().n_valid(), 0);
    let again: Vec<_> = [KANJI, 'B' as u32].iter().map(|&cp| draw(&mut r.engine, cp, Cell::Full)).collect();
    assert_eq!(again, cold, "regenerated after trim");
}

#[test]
fn fallback_entries_are_committed_and_hit() {
    let mut r = rig(8);
    let (res, g, bits) = draw(&mut r.engine, NOWHERE, Cell::Full);
    assert_eq!((res, g.entry.kind, g.cached, bits.len()), (Resolved::Missing, Kind::Missing, true, 0));
    let (res, g, _) = draw(&mut r.engine, SPACE, Cell::Half);
    assert_eq!((res, g.entry.kind, g.entry.face), (Resolved::Space, Kind::Space, FaceId::Latin));
    let (res, g, _) = draw(&mut r.engine, HUGE, Cell::Half);
    assert_eq!((res, g.entry.kind, g.cached), (Resolved::Kcg, Kind::Kcg, true));
    assert_eq!(r.engine.stats().raster_err, 1);
    assert_eq!(r.engine.stats().cmap_missing, 1);
    let before = reads(&r);
    for (cp, cell, want) in [(NOWHERE, Cell::Full, Resolved::Missing), (SPACE, Cell::Half, Resolved::Space), (HUGE, Cell::Half, Resolved::Kcg)] {
        assert_eq!(draw(&mut r.engine, cp, cell).0, want);
    }
    assert_eq!(reads(&r), before, "fallback hits search no cmap");
    assert_eq!(r.engine.stats().hit_fallback, 3);
    assert_eq!(r.engine.stats().commit_bitmap, 0);
    assert_eq!(r.engine.cache().n_valid(), 3);
}

#[test]
fn over_slot_and_no_arena_are_uncached_but_complete() {
    let mut r = rig(8);
    let (res, g, bits) = draw(&mut r.engine, WIDE, Cell::Half);
    assert_eq!((res, g.cached, g.entry.width, g.entry.pitch), (Resolved::Bitmap, false, 25, 4));
    assert_eq!(bits.len(), 4 * 8);
    assert_eq!((r.engine.stats().uncached, r.engine.cache().n_valid()), (1, 0));
    // No arena (allocation failed): every bitmap complete from the scratch, valid 0.
    let mut bare = rig(0);
    let (res, g, bits) = draw(&mut bare.engine, KANJI, Cell::Full);
    let (_, _, cached_bits) = draw(&mut r.engine, KANJI, Cell::Full);
    assert_eq!((res, g.cached), (Resolved::Bitmap, false));
    assert_eq!(bits, cached_bits);
    assert_eq!(bare.engine.cache().n_valid(), 0);
    assert_eq!(draw(&mut bare.engine, NOWHERE, Cell::Full).0, Resolved::Missing);
}

#[test]
fn faults_publish_no_partial_bitmap() {
    // ③: a failing read (Err or short) during the glyph read -> one complete
    // Kcg entry, no bitmap entry, nothing borrowed; retried only after invalidation.
    for short in [false, true] {
        let mut r = rig(8);
        draw(&mut r.engine, 'A' as u32, Cell::Half);
        let at = Some(r.latin.reads.get() + 1);
        if short {
            r.latin.short_at.set(at);
        } else {
            r.latin.fail_at.set(at);
        }
        r.engine.trim().unwrap(); // the glyph read goes to the source
        let (res, g, bits) = draw(&mut r.engine, 'B' as u32, Cell::Half);
        assert_eq!((res, g.entry.kind, bits.len()), (Resolved::Kcg, Kind::Kcg, 0), "short={short}");
        assert_eq!((r.engine.stats().commit_bitmap, r.engine.cache().n_valid()), (1, 1));
        assert!(!r.engine.is_busy());
        r.latin.short_at.set(None);
        r.latin.fail_at.set(None);
        assert_eq!(draw(&mut r.engine, 'B' as u32, Cell::Half).0, Resolved::Kcg, "kept until invalidation");
        r.engine.invalidate_all().unwrap();
        let (res, g, _) = draw(&mut r.engine, 'B' as u32, Cell::Half);
        assert_eq!((res, g.entry.height), (Resolved::Bitmap, 15));
    }
}

#[test]
fn borrowed_bitmap_blocks_release_paths() {
    // b3⑤: while a bitmap is borrowed nothing that frees or rewrites it runs.
    let mut r = rig(1);
    let mut g = Glyph::default();
    assert_eq!(r.engine.resolve('A' as u32, Cell::Half, Px16, &mut g), Resolved::Bitmap);
    let held = r.engine.bitmap(&g).to_vec();
    assert_eq!(r.engine.trim(), Err(EngineError::Busy));
    assert_eq!(r.engine.close(), Err(EngineError::Busy));
    assert_eq!(r.engine.invalidate_all(), Err(EngineError::Busy));
    assert_eq!(r.engine.set_kcg_only(Reason::Config), Err(EngineError::Busy));
    assert_eq!(r.engine.detach_arena().map(|a| a.is_some()), Err(EngineError::Busy));
    assert!(r.engine.attach_arena(arena(SLOT16_BYTES), 1, 0).is_err());
    assert!(!r.engine.begin(), "no entry check while borrowed");
    // A second resolve would evict the borrowed slot (1 slot): refused.
    let mut other = Glyph::default();
    assert_eq!(r.engine.resolve(KANJI, Cell::Full, Px16, &mut other), Resolved::Kcg);
    assert_eq!(r.engine.stats().busy, 1);
    assert_eq!((r.engine.state(), r.engine.cache().n_valid()), (State::Ready, 1));
    assert_eq!(r.engine.bitmap(&g), &held[..]);
    r.engine.release();
    assert!(r.engine.bitmap(&g).is_empty(), "no access after release");
    assert!(r.engine.begin());
    assert_eq!(r.engine.trim(), Ok(()));
    assert_eq!(r.engine.cache().n_valid(), 0);
}

#[test]
fn entry_check_once_and_stale() {
    let mut r = rig(8);
    assert_eq!((r.latin.validates.get(), r.ja.validates.get()), (1, 1));
    for cp in [KANJI, KANA, 'A' as u32, KANJI] {
        draw(&mut r.engine, cp, Cell::Full);
    }
    assert_eq!((r.latin.validates.get(), r.ja.validates.get()), (1, 1), "not per glyph");
    r.engine.end();
    let mut g = Glyph::default();
    assert_eq!(r.engine.resolve(KANJI, Cell::Full, Px16, &mut g), Resolved::Kcg, "outside begin..end");
    assert_eq!(r.engine.peek(KANJI, Px16, &mut g), Resolved::Kcg);
    assert!(r.engine.begin());
    assert_eq!(r.engine.peek(KANJI, Px16, &mut g), Resolved::Bitmap);
    r.engine.release();
    r.ja.stale.set(true);
    assert!(!r.engine.begin());
    assert_eq!(r.engine.state(), State::KcgOnly(Reason::Stale));
    assert_eq!(r.engine.cache().n_valid(), 0);
    assert!(r.ja.log.borrow().contains(&"close ja".to_string()));
    assert!(r.latin.log.borrow().contains(&"close latin".to_string()));
    assert_eq!(draw(&mut r.engine, KANJI, Cell::Full).0, Resolved::Kcg);
    assert_eq!(r.engine.stats().stale, 1);
    assert!(!r.engine.begin(), "stays KCG until reopened");
}

#[test]
fn reopen_closes_invalidates_then_opens() {
    // b3②: close old faces -> invalidate_all -> set_slot += 1 -> open new.
    let mut r = rig(8);
    for cp in [KANJI, KANA] {
        draw(&mut r.engine, cp, Cell::Full);
    }
    let slot = r.engine.set_slot();
    r.engine.end();
    let probe = Rc::new(Probe::default());
    // One shared probe orders both generations' events.
    r.engine.close().unwrap();
    assert_eq!(r.engine.cache().n_valid(), 0, "invalidated with the close");
    r.engine.open(src("latin2", latin_font(), &probe), src("ja2", ja_font(), &probe)).unwrap();
    assert_eq!(r.engine.set_slot(), slot.wrapping_add(1));
    assert!(r.ja.log.borrow().contains(&"close ja".to_string()));
    // Reopen in place without close: old sources dropped before the new ones read.
    let mut e2: Box<Engine<Memory>> = Box::new(Engine::new());
    e2.attach_arena(arena(8 * SLOT16_BYTES), 8, 0).unwrap();
    e2.open(src("old-latin", latin_font(), &probe), src("old-ja", ja_font(), &probe)).unwrap();
    assert!(e2.begin());
    draw(&mut e2, KANJI, Cell::Full);
    e2.end();
    assert_eq!(e2.cache().n_valid(), 1);
    probe.log.borrow_mut().clear();
    e2.open(src("new-latin", latin_font(), &probe), src("new-ja", ja_font(), &probe)).unwrap();
    let log = probe.log.borrow().clone();
    let pos = |s: &str| log.iter().position(|x| x == s).unwrap();
    assert!(pos("close old-latin") < pos("new-latin") && pos("close old-ja") < pos("new-latin"), "{log:?}");
    assert_eq!(e2.cache().n_valid(), 0, "old generation invalidated");
    assert!(e2.begin());
    let mut g = Glyph::default();
    assert_eq!(e2.peek(KANJI, Px16, &mut g), Resolved::Miss);
}

#[test]
fn open_failure_is_kcg_only() {
    let probe = Rc::new(Probe::default());
    let mut e: Box<Engine<Memory>> = Box::new(Engine::new());
    let mut broken = ja_font();
    broken[0] = 0x7f;
    let got = e.open(src("latin", latin_font(), &probe), src("ja", broken, &probe));
    assert_eq!(got, Err(EngineError::Font(Error::Unsupported)));
    assert_eq!(e.state(), State::KcgOnly(Reason::OpenFailed));
    assert!(probe.log.borrow().contains(&"close latin".to_string()), "both closed");
    assert!(!e.begin());
    assert_eq!(draw(&mut e, 'A' as u32, Cell::Half).0, Resolved::Kcg);
    e.set_kcg_only(Reason::Config).unwrap();
    assert_eq!(e.state(), State::KcgOnly(Reason::Config));
}

#[test]
fn need_raster_only_for_misses() {
    let mut r = rig(8);
    assert!(r.engine.need_raster(Px16, [KANJI, KANA]));
    draw(&mut r.engine, KANJI, Cell::Full);
    assert!(r.engine.need_raster(Px16, [KANJI, KANA]));
    draw(&mut r.engine, KANA, Cell::Full);
    assert!(!r.engine.need_raster(Px16, [KANJI, KANA]));
    r.engine.end();
    assert!(!r.engine.need_raster(Px16, [NOWHERE]), "no raster outside begin..end");
}

struct Nothing;
impl Source for Nothing {
    fn file_size(&self) -> u64 {
        0
    }
    fn read_at(&mut self, _: u64, _: &mut [u8]) -> Result<usize> {
        Err(Error::Io)
    }
}
static NEW: Engine<Nothing> = Engine::new();

#[test]
fn zero_state_equals_new() {
    // P2-10: the app zero-fills its exec_heap block; that must be Engine::new().
    let size = std::mem::size_of::<Engine<Nothing>>();
    let bytes = unsafe { std::slice::from_raw_parts(&NEW as *const _ as *const u8, size) };
    assert!(bytes.iter().all(|&b| b == 0), "Engine::new() is all zero");
    let layout = std::alloc::Layout::new::<Engine<Nothing>>();
    let e = unsafe { Box::from_raw(std::alloc::alloc_zeroed(layout) as *mut Engine<Nothing>) };
    assert_eq!((e.state(), e.set_slot(), e.is_busy(), e.cache().has_arena()), (State::Closed, 0, false, false));
    println!("Engine<_> size {size} B (2 faces + scratch + cache context)");
}

// ---- real fonts: per-face baseline (U1 (a)) and slot fit ---------------------

struct Disk(File, u64);
impl Source for Disk {
    fn file_size(&self) -> u64 {
        self.1
    }
    fn read_at(&mut self, at: u64, dst: &mut [u8]) -> Result<usize> {
        self.0.read_at(dst, at).map_err(|_| Error::Io)
    }
}
fn disk(path: PathBuf) -> Disk {
    let f = File::open(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
    let n = f.metadata().unwrap().len();
    Disk(f, n)
}

#[derive(Default, Debug)]
struct Span {
    glyphs: usize,
    /// Inked rows above / below the baseline (the control box may add an empty
    /// row: Shinonome's dot grid sits 0.25px off the pixel grid).
    top: i16,
    below: i16,
    bbox_below: i16,
    over_left: usize,
    over_right: usize,
    /// Bitmaps that did not fit their slot (drawn uncached).
    uncached: usize,
}
/// Inked rows (above, below) the baseline of one bitmap.
fn ink(g: &Glyph, bits: &[u8]) -> Option<(i16, i16)> {
    let pitch = usize::from(g.entry.pitch);
    let rows: Vec<usize> = (0..usize::from(g.entry.height)).filter(|r| bits[r * pitch..(r + 1) * pitch].iter().any(|&b| b != 0)).collect();
    let (first, last) = (*rows.first()? as i16, *rows.last()? as i16);
    Some((g.entry.top - first, last + 1 - g.entry.top))
}
fn measure(e: &mut Engine<Disk>, cps: impl Iterator<Item = u32>, cell: Cell, face: FaceId, cell_w: i16) -> Span {
    let mut s = Span::default();
    for cp in cps {
        let mut g = Glyph::default();
        let r = e.resolve(cp, cell, Px16, &mut g);
        let inked = ink(&g, e.bitmap(&g));
        e.release();
        if r != Resolved::Bitmap || g.entry.face != face {
            continue;
        }
        s.glyphs += 1;
        if let Some((top, below)) = inked {
            s.top = s.top.max(top);
            s.below = s.below.max(below);
        }
        s.bbox_below = s.bbox_below.max(g.entry.height as i16 - g.entry.top);
        s.over_left += usize::from(g.entry.left < 0);
        s.over_right += usize::from(g.entry.left + g.entry.width as i16 > cell_w);
        s.uncached += usize::from(!g.cached);
    }
    s
}

#[test]
fn real_fonts_ascent_and_slot_fit() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../assets/fonts");
    let jf = PathBuf::from(std::env::var("OS32_FONT_SHINONOME").expect("OS32_FONT_SHINONOME"));
    let mut e: Box<Engine<Disk>> = Box::new(Engine::new());
    e.attach_arena(arena(ARENA_BYTES), N16, N32).unwrap();
    e.open(disk(root.join("Inconsolata-Regular.ttf")), disk(jf)).unwrap();
    assert!(e.begin());
    let below = CELL_H_PX as i16 - ASCENT_LATIN_PX;
    let ascii = measure(&mut e, 0x21..0x7f, Cell::Half, FaceId::Latin, 8);
    println!("Inconsolata ASCII: {ascii:?}");
    assert_eq!(ascii.glyphs, 94);
    assert!(ascii.top <= ASCENT_LATIN_PX, "ASCII top {} rows > ascent", ascii.top);
    // 'j' etc. reach 3 rows below: ascent 13 is the only per-face baseline without a cut.
    assert_eq!(ascii.below, below, "ASCII rows below the baseline");
    let latin1 = measure(&mut e, 0xa1..0x100, Cell::Half, FaceId::Latin, 8);
    let mut cut = Vec::new();
    for cp in 0xa1..0x100u32 {
        let mut g = Glyph::default();
        if e.resolve(cp, Cell::Half, Px16, &mut g) == Resolved::Bitmap {
            if let Some((top, under)) = ink(&g, e.bitmap(&g)) {
                if top > ASCENT_LATIN_PX || under > below {
                    cut.push(format!("U+{cp:04X}"));
                }
            }
        }
        e.release();
    }
    println!("Inconsolata Latin-1: {latin1:?}; outside the 8x16 cell vertically: {} {cut:?}", cut.len());
    // Measured 2026-10-20: the control box of c-cedilla (yMin -3.15) has a 4th row
    // below, but its coverage stays under the threshold, so no Latin-1 ink leaves
    // the 8x16 cell with ascent 13 (A-ring 12.72 / E-acute 12.66 included).
    assert!(cut.is_empty(), "Latin-1 ink outside the cell: {cut:?}");
    assert_eq!(latin1.bbox_below, below + 1, "control box of the c-cedilla group");
    let jis = measure(&mut e, 0x20..0x10000, Cell::Full, FaceId::Ja, 16);
    println!("Shinonome (Full, ja face): {jis:?}");
    assert!(jis.glyphs > 6000, "corpus size");
    assert_eq!(jis.top, ASCENT_JA_PX, "Shinonome rows above the baseline");
    assert_eq!(jis.below, CELL_H_PX as i16 - ASCENT_JA_PX, "Shinonome rows below the baseline");
    assert_eq!((jis.over_left, jis.over_right), (0, 0), "Shinonome stays in its 16px cell");
    // Standard font set corpus (Shinonome, Inconsolata ASCII/Latin-1): no glyph
    // exceeds its slot. Inconsolata's own box drawing/blocks (reached only as the
    // fallback for codepoints Shinonome lacks) are 23 rows: uncached, counted.
    assert_eq!((ascii.uncached, latin1.uncached, jis.uncached), (0, 0, 0));
    println!("stats over U+0020..U+FFFF: {:?}", e.stats());
    // Warm hit after evictions == a fresh raster.
    let mut g = Glyph::default();
    e.invalidate_all().unwrap();
    assert_eq!(e.resolve(0x8b70, Cell::Full, Px16, &mut g), Resolved::Bitmap);
    let cold = e.bitmap(&g).to_vec();
    e.release();
    assert_eq!(e.resolve(0x8b70, Cell::Full, Px16, &mut g), Resolved::Bitmap);
    assert_eq!((e.bitmap(&g), g.cached), (&cold[..], true));
    e.release();
}
