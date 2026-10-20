//! TASK_FONTPERF F3 (fontperf design §2, §4, §7): the guest reader of the .g16
//! bundle over the real JF Dot Shinonome 16 / Inconsolata and the bundle the
//! product generators made from that TTF (OS32_FONT_G16, test_font_engine.py):
//! every record resolves like the TTF path (Entry and cell bitmap) with one
//! read_at, the 7 rejections (magic, version, cell/ascent, head, index CRC,
//! short file, short tail) drop the source and keep the TTF path, a preload
//! record whose cp the index lacks is not committed, 8 consecutive read failures
//! close the bundle, 32px never looks at it (F-1), every face close path drops
//! its source and 20 reopenings keep the source count (F-8), and the preload
//! counts (bundle list + ASCII).
use os32_font::{
    bundle::Reject,
    cache::{FaceId, GlyphKey, Kind},
    engine::{Cell, Engine, EngineError, Glyph, Resolved, State},
    limits::*,
    raster::PixelSize::{Px16, Px32},
    reader::{Error, Result, Source},
};
use std::{cell::Cell as StdCell, path::PathBuf, rc::Rc};

#[derive(Default)]
struct Probe {
    reads: StdCell<usize>,
    live: StdCell<isize>,
    /// Every read_at fails while set.
    fail: StdCell<bool>,
    /// validate fails while set (Stale).
    stale: StdCell<bool>,
}
struct Mem {
    data: Rc<Vec<u8>>,
    probe: Rc<Probe>,
}
impl Source for Mem {
    fn file_size(&self) -> u64 {
        self.data.len() as u64
    }
    fn read_at(&mut self, at: u64, dst: &mut [u8]) -> Result<usize> {
        let p = &self.probe;
        p.reads.set(p.reads.get() + 1);
        if p.fail.get() {
            return Err(Error::Io);
        }
        let at = at as usize;
        let n = dst.len().min(self.data.len().saturating_sub(at));
        dst[..n].copy_from_slice(&self.data[at..at + n]);
        Ok(n)
    }
    fn validate(&mut self) -> Result<()> {
        if self.probe.stale.get() {
            Err(Error::Io)
        } else {
            Ok(())
        }
    }
}
impl Drop for Mem {
    fn drop(&mut self) {
        self.probe.live.set(self.probe.live.get() - 1);
    }
}

struct Files {
    latin: Rc<Vec<u8>>,
    ja: Rc<Vec<u8>>,
    g16: Rc<Vec<u8>>,
}
fn files() -> Files {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../assets/fonts");
    let read = |p: PathBuf| Rc::new(std::fs::read(&p).unwrap_or_else(|e| panic!("{}: {e}", p.display())));
    Files {
        latin: read(root.join("Inconsolata-Regular.ttf")),
        ja: read(PathBuf::from(std::env::var("OS32_FONT_SHINONOME").expect("OS32_FONT_SHINONOME"))),
        g16: read(PathBuf::from(std::env::var("OS32_FONT_G16").expect("OS32_FONT_G16"))),
    }
}
fn mem(data: &Rc<Vec<u8>>, probe: &Rc<Probe>) -> Mem {
    probe.live.set(probe.live.get() + 1);
    Mem {
        data: data.clone(),
        probe: probe.clone(),
    }
}
fn arena(bytes: usize) -> &'static mut [u8] {
    Box::leak(vec![0u8; bytes].into_boxed_slice())
}

struct Rig {
    e: Box<Engine<Mem>>,
    faces: Rc<Probe>,
    g16: Rc<Probe>,
}
/// Engine with the 128KiB layout and the standard faces open (no bundle yet).
fn rig(f: &Files) -> Rig {
    let faces = Rc::new(Probe::default());
    let g16 = Rc::new(Probe::default());
    let mut e: Box<Engine<Mem>> = Box::new(Engine::new());
    let (n_cell, n16) = layout(ARENA_BYTES_SHELL);
    e.attach_arena(arena(ARENA_BYTES_SHELL), n_cell, n16, N32).unwrap();
    e.open(mem(&f.latin, &faces), mem(&f.ja, &faces)).unwrap();
    Rig { e, faces, g16 }
}
fn with_bundle(f: &Files, data: &Rc<Vec<u8>>) -> Rig {
    let mut r = rig(f);
    let src = mem(data, &r.g16);
    r.e.open_bundle(src).unwrap();
    r
}

fn le32(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(b[at..at + 4].try_into().unwrap())
}
/// Every cp of the bundle (from its rank bitmap), ascending.
fn bundle_cps(g: &[u8]) -> Vec<u32> {
    let off = le32(g, 68) as usize;
    (0..G16_BMP_CPS as u32).filter(|&cp| g[off + (cp as usize >> 3)] >> (cp & 7) & 1 != 0).collect()
}
/// The preload list (cp of each preload record, in order).
fn preload_cps(g: &[u8]) -> Vec<u32> {
    let (n_pre, off_pre) = (le32(g, 16) as usize, le32(g, 80) as usize);
    (0..n_pre).map(|i| le32(g, off_pre + i * G16_PRE_REC)).collect()
}

fn draw(e: &mut Engine<Mem>, cp: u32, cell: Cell, px: os32_font::raster::PixelSize) -> (Resolved, Glyph, Vec<u8>) {
    let mut g = Glyph::default();
    let r = e.resolve(cp, cell, px, &mut g);
    let bits = e.bitmap(&g).to_vec();
    e.release();
    (r, g, bits)
}

#[test]
fn bundle_resolves_like_the_ttf_with_one_read() {
    let f = files();
    let cps = bundle_cps(&f.g16);
    let mut ttf = rig(&f);
    let mut b = with_bundle(&f, &f.g16);
    assert!(b.e.bundle_ok());
    let (n_all, n_pre) = b.e.bundle_counts();
    assert_eq!(n_all as usize, cps.len());
    println!("g16: n_all={n_all} n_pre={n_pre} records compared={}", cps.len());
    assert!(ttf.e.begin() && b.e.begin());
    let mut multi = 0;
    for &cp in &cps {
        let (rt, gt, bt) = draw(&mut ttf.e, cp, Cell::Full, Px16);
        let before = b.g16.reads.get() + b.faces.reads.get();
        let (rb, gb, bb) = draw(&mut b.e, cp, Cell::Full, Px16);
        let reads = b.g16.reads.get() + b.faces.reads.get() - before;
        assert_eq!((rt, gt.entry, gt.ascent), (rb, gb.entry, gb.ascent), "U+{cp:04X} entry");
        assert_eq!(bt, bb, "U+{cp:04X} bitmap");
        if rb == Resolved::Bitmap {
            assert_eq!(gb.entry.face, FaceId::Ja);
        }
        if reads != 1 {
            multi += 1;
        }
    }
    assert_eq!(multi, 0, "a bundle miss must cost exactly one read_at");
    let s = b.e.bundle_stats();
    assert_eq!((s.ok, s.rejected, s.hit, s.err), (1, 0, n_all, 0));
    assert_eq!(b.e.stats().raster, 0, "no raster with the bundle");
    // A Full cp the bundle lacks goes the TTF way (and counts a bundle miss).
    let (r, _, _) = draw(&mut b.e, 0x2F00, Cell::Full, Px16);
    assert_eq!(b.e.bundle_stats().miss, 1);
    let (rt, _, _) = draw(&mut ttf.e, 0x2F00, Cell::Full, Px16);
    assert_eq!(r, rt);
    ttf.e.end();
    b.e.end();
}

/// (label, mutate the bundle bytes, expected reason)
fn rejects(g: &[u8]) -> Vec<(&'static str, Vec<u8>, Reject)> {
    let mut out = Vec::new();
    let mut m = |label, f: &dyn Fn(&mut Vec<u8>), why| {
        let mut v = g.to_vec();
        f(&mut v);
        out.push((label, v, why));
    };
    m("magic", &|v| v[0] ^= 1, Reject::Magic);
    m("version", &|v| v[4] = 2, Reject::Version);
    m("cell", &|v| v[8] = 24, Reject::Layout);
    m("ascent", &|v| v[10] = 13, Reject::Layout);
    m("head", &|v| v[24] ^= 1, Reject::Head);
    m("index-crc", &|v| {
        let off = le32(v, 68) as usize;
        v[off + 0x4E9C / 8 + 1] ^= 0x10;
    }, Reject::IndexCrc);
    m("short-file", &|v| v.truncate(100), Reject::Io);
    m("short-tail", &|v| {
        let n = v.len() - 1;
        v.truncate(n);
    }, Reject::Layout);
    out
}

#[test]
fn rejected_bundles_drop_the_source_and_keep_the_ttf() {
    let f = files();
    for (label, data, why) in rejects(&f.g16) {
        let mut r = rig(&f);
        let src = mem(&Rc::new(data), &r.g16);
        assert_eq!(r.e.open_bundle(src), Err(why), "{label}");
        assert!(!r.e.bundle_ok() && !r.e.bundle_has_source(), "{label}");
        assert_eq!(r.g16.live.get(), 0, "{label}: source kept");
        let s = r.e.bundle_stats();
        assert_eq!((s.ok, s.rejected), (0, why as u32), "{label}");
        assert_eq!(r.e.state(), State::Ready, "{label}");
        assert!(r.e.begin());
        let reads = r.g16.reads.get();
        assert_eq!(draw(&mut r.e, 0x4E9C, Cell::Full, Px16).0, Resolved::Bitmap, "{label}");
        assert_eq!(r.g16.reads.get(), reads, "{label}: read a rejected bundle");
        assert_eq!(r.e.stats().raster, 1, "{label}: TTF path");
        r.e.end();
        println!("reject {label}: {why:?}");
    }
    // A closed set refuses the bundle (NotOpen) and drops it.
    let mut r = rig(&f);
    r.e.close().unwrap();
    let src = mem(&f.g16, &r.g16);
    assert_eq!(r.e.open_bundle(src), Err(Reject::NotOpen));
    assert_eq!(r.g16.live.get(), 0);
}

#[test]
fn preload_skips_records_whose_cp_the_index_lacks() {
    let f = files();
    let mut data = f.g16.to_vec();
    let off_pre = le32(&data, 80) as usize;
    let first = le32(&data, off_pre);
    // 'A' is not on the Full path: the bundle has no record for it.
    data[off_pre..off_pre + 4].copy_from_slice(&0x41u32.to_le_bytes());
    let mut r = with_bundle(&f, &Rc::new(data));
    let n_pre = r.e.bundle_counts().1;
    let n = r.e.preload_from_bundle(usize::MAX).unwrap();
    assert_eq!(n, n_pre - 1);
    assert_eq!(r.e.bundle_stats().err, 1);
    let key = |cp| GlyphKey { set: r.e.set_slot(), cp, px: RASTER_PPEM as u8 };
    assert!(!r.e.cache().contains(key(0x41)), "a record of a cp the index lacks was committed");
    assert!(!r.e.cache().contains(key(first)));
}

#[test]
fn eight_failed_reads_close_the_bundle() {
    let f = files();
    let mut r = with_bundle(&f, &f.g16);
    assert!(r.e.begin());
    let cps = bundle_cps(&f.g16);
    r.g16.fail.set(true);
    for (i, &cp) in cps.iter().take(G16_ERR_CLOSE as usize).enumerate() {
        // Each failure falls back to the TTF (same glyph), the 8th closes the bundle.
        assert_eq!(draw(&mut r.e, cp, Cell::Full, Px16).0, Resolved::Bitmap);
        assert_eq!(r.e.bundle_ok(), i + 1 < G16_ERR_CLOSE as usize);
    }
    assert_eq!(r.e.bundle_stats().err, u32::from(G16_ERR_CLOSE));
    assert!(!r.e.bundle_has_source() && r.g16.live.get() == 0, "closed bundle kept its source");
    let reads = r.g16.reads.get();
    assert_eq!(draw(&mut r.e, cps[100], Cell::Full, Px16).0, Resolved::Bitmap);
    assert_eq!(r.g16.reads.get(), reads, "read a closed bundle");
    r.e.end();
}

#[test]
fn px32_never_reads_the_bundle() {
    let f = files();
    let mut r = with_bundle(&f, &f.g16);
    assert!(r.e.begin());
    let reads = r.g16.reads.get();
    for cp in [0x4E9C, 0x3042, 0x6F22, 0xFF21] {
        let (res, g, _) = draw(&mut r.e, cp, Cell::Full, Px32);
        assert_eq!(res, Resolved::Bitmap);
        assert_eq!(g.entry.face, FaceId::Ja);
    }
    let s = r.e.bundle_stats();
    assert_eq!((s.hit, s.miss, s.err), (0, 0, 0), "32px looked at the bundle");
    assert_eq!(r.g16.reads.get(), reads);
    assert_eq!(r.e.stats().raster, 4);
    r.e.end();
}

#[test]
fn every_close_path_drops_the_bundle_source() {
    let f = files();
    // F-8: close, set_kcg_only, the Stale of begin, reopen.
    let mut r = with_bundle(&f, &f.g16);
    r.e.close().unwrap();
    assert_eq!((r.g16.live.get(), r.e.bundle_has_source()), (0, false), "close");
    let mut r = with_bundle(&f, &f.g16);
    r.e.set_kcg_only(os32_font::engine::Reason::Config).unwrap();
    assert_eq!(r.g16.live.get(), 0, "set_kcg_only");
    let mut r = with_bundle(&f, &f.g16);
    r.faces.stale.set(true);
    assert!(!r.e.begin());
    assert_eq!((r.e.state(), r.g16.live.get()), (State::KcgOnly(os32_font::engine::Reason::Stale), 0), "stale");
    // Busy: nothing closes while a bitmap is borrowed.
    let mut r = with_bundle(&f, &f.g16);
    assert!(r.e.begin());
    let mut g = Glyph::default();
    assert_eq!(r.e.resolve(0x4E9C, Cell::Full, Px16, &mut g), Resolved::Bitmap);
    assert_eq!(r.e.close(), Err(EngineError::Busy));
    assert_eq!(r.g16.live.get(), 1);
    r.e.release();
    r.e.end();
    // 20 setting changes (reopen + bundle): the live source count stays 3, each
    // reopen drops the 3 old ones (trim keeps them).
    for round in 0..20 {
        r.e.open(mem(&f.latin, &r.faces), mem(&f.ja, &r.faces)).unwrap();
        let src = mem(&f.g16, &r.g16);
        r.e.open_bundle(src).unwrap();
        assert_eq!((r.faces.live.get(), r.g16.live.get()), (2, 1), "round {round}");
        r.e.trim().unwrap();
        assert_eq!(r.g16.live.get(), 1, "trim dropped the bundle");
    }
    r.e.close().unwrap();
    assert_eq!((r.faces.live.get(), r.g16.live.get()), (0, 0));
}

#[test]
fn preload_counts_and_hits() {
    let f = files();
    let list = preload_cps(&f.g16);
    let mut r = with_bundle(&f, &f.g16);
    let n_pre = r.e.bundle_counts().1 as usize;
    assert_eq!(n_pre, list.len());
    assert!(n_pre <= SHELL_PRELOAD_MAX && n_pre > 0);
    let reads = r.g16.reads.get();
    let n = r.e.preload_from_bundle(SHELL_PRELOAD_MAX).unwrap() as usize;
    let per_read = G16_READ_BYTES / G16_PRE_REC;
    assert_eq!(n, n_pre, "preload count");
    assert_eq!(r.g16.reads.get() - reads, n_pre.div_ceil(per_read), "preload reads");
    let ascii = r.e.preload_ascii().unwrap() as usize;
    assert_eq!(ascii, (PRELOAD_ASCII_FIRST..=PRELOAD_ASCII_LAST).count());
    assert_eq!(r.e.cache().n_valid(), n_pre + ascii);
    println!("preload: bundle {n} in {} reads, ascii {ascii}", n_pre.div_ceil(per_read));
    // Every listed glyph and every ASCII glyph hits without a read.
    assert!(r.e.begin());
    let (fr, gr, miss) = (r.faces.reads.get(), r.g16.reads.get(), r.e.stats().miss);
    for &cp in &list {
        let (res, g, bits) = draw(&mut r.e, cp, Cell::Full, Px16);
        let ok = match res {
            Resolved::Bitmap => bits.len() == G16_ALL_REC,
            Resolved::Space => bits.is_empty(),
            _ => false,
        };
        assert!(ok && g.cached, "U+{cp:04X} {res:?}");
    }
    for cp in PRELOAD_ASCII_FIRST..=PRELOAD_ASCII_LAST {
        let (res, g, _) = draw(&mut r.e, cp, Cell::Half, Px16);
        assert!(g.cached && matches!(res, Resolved::Bitmap | Resolved::Space), "{cp:#x}");
    }
    assert_eq!((r.faces.reads.get(), r.g16.reads.get()), (fr, gr), "a preloaded glyph read");
    assert_eq!(r.e.stats().miss, miss);
    // The preload matches what a miss would resolve (the all-section record).
    let mut cold = with_bundle(&f, &f.g16);
    assert!(cold.e.begin());
    for &cp in list.iter().step_by(37) {
        let a = draw(&mut r.e, cp, Cell::Full, Px16);
        let b = draw(&mut cold.e, cp, Cell::Full, Px16);
        assert_eq!((a.1.entry, a.2), (b.1.entry, b.2), "U+{cp:04X}");
    }
    cold.e.end();
    // Busy refuses; a limit preloads that many.
    let mut g = Glyph::default();
    assert_eq!(r.e.resolve(0x3042, Cell::Full, Px16, &mut g), Resolved::Bitmap);
    assert_eq!(r.e.preload_from_bundle(10), Err(EngineError::Busy));
    assert_eq!(r.e.preload_ascii(), Err(EngineError::Busy));
    r.e.release();
    r.e.end();
    let mut small = with_bundle(&f, &f.g16);
    assert_eq!(small.e.preload_from_bundle(10).unwrap(), 10);
    assert_eq!(small.e.cache().n_valid(), 10);
    assert!(matches!(small.e.cache().entry(0).unwrap().kind, Kind::Bitmap | Kind::Space));
}
