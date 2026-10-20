//! T7b-a5 raster: real corpora (dumped for test_font_raster.py's independent
//! references) and synthetic bounds/fault/commit cases on the product path.
use os32_font::{
    limits::*,
    raster::{units_to_26_6, PixelSize, Placement, RasterScratch},
    reader::{Error, Face, Result, Source},
};

/// T7b-a7: a Face is opened in place in caller-owned storage (here a Box).
fn open_face<S: Source>(source: S) -> Result<Box<Face<S>>> {
    let mut face = Box::new(Face::new());
    face.open(source)?;
    Ok(face)
}
use std::{
    cell::Cell,
    collections::BTreeMap,
    fmt::Write as _,
    fs::{self, File},
    os::unix::fs::FileExt,
    path::PathBuf,
    rc::Rc,
};

#[derive(Default)]
struct Reads {
    count: Cell<usize>,
    fail: Cell<Option<usize>>,
}
struct Memory {
    data: Vec<u8>,
    reads: Rc<Reads>,
}
impl Source for Memory {
    fn file_size(&self) -> u64 {
        self.data.len() as u64
    }
    fn read_at(&mut self, at: u64, dst: &mut [u8]) -> Result<usize> {
        self.reads.count.set(self.reads.count.get() + 1);
        if self.reads.fail.get() == Some(self.reads.count.get()) {
            return Err(Error::Io);
        }
        dst.copy_from_slice(&self.data[at as usize..at as usize + dst.len()]);
        Ok(dst.len())
    }
}
struct Disk(File, u64);
impl Source for Disk {
    fn file_size(&self) -> u64 {
        self.1
    }
    fn read_at(&mut self, at: u64, dst: &mut [u8]) -> Result<usize> {
        self.0.read_at(dst, at).map_err(|_| Error::Io)
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

// Valid synthetic SFNT (same layout as tests/outline.rs) with a chosen upem.
fn fixture(upem: u16, glyphs: Vec<Vec<u8>>) -> Vec<u8> {
    let n = glyphs.len() as u16;
    let mut parts = BTreeMap::new();
    let mut head = vec![0; 54];
    w32(&mut head, 0, 0x10000);
    w32(&mut head, 12, 0x5f0f3cf5);
    w16(&mut head, 18, upem);
    w16(&mut head, 50, 1);
    let mut hhea = vec![0; 36];
    w32(&mut hhea, 0, 0x10000);
    w16(&mut hhea, 34, 1);
    let mut maxp = vec![0; 32];
    w32(&mut maxp, 0, 0x10000);
    w16(&mut maxp, 4, n);
    let mut cmap = vec![0; 40];
    w16(&mut cmap, 2, 1);
    w16(&mut cmap, 4, 3);
    w16(&mut cmap, 6, 10);
    w32(&mut cmap, 8, 12);
    w16(&mut cmap, 12, 12);
    w32(&mut cmap, 16, 28);
    w32(&mut cmap, 24, 1);
    w32(&mut cmap, 28, 65);
    w32(&mut cmap, 32, 65);
    w32(&mut cmap, 36, 1);
    let mut glyf = Vec::new();
    let mut loca = Vec::new();
    for g in glyphs {
        loca.extend((glyf.len() as u32).to_be_bytes());
        glyf.extend(g);
    }
    loca.extend((glyf.len() as u32).to_be_bytes());
    let mut hmtx = vec![0; 4 + (n as usize - 1) * 2];
    w16(&mut hmtx, 0, upem); // advance 1 em for every glyph
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

/// Simple glyph from absolute (x, y, on_curve) points per contour (word deltas).
fn glyph(contours: &[Vec<(i32, i32, bool)>]) -> Vec<u8> {
    let points: Vec<_> = contours.iter().flatten().copied().collect();
    let mut b = vec![0; 10];
    w16(&mut b, 0, contours.len() as u16);
    let mut end = 0;
    for c in contours {
        end += c.len();
        word(&mut b, (end - 1) as u16);
    }
    word(&mut b, 0);
    for p in &points {
        b.push(if p.2 { 1 } else { 0 });
    }
    let (mut x, mut y) = (0, 0);
    let mut ys = Vec::new();
    for p in &points {
        word(&mut b, (p.0 - x) as i16 as u16);
        ys.push((p.1 - y) as i16 as u16);
        x = p.0;
        y = p.1;
    }
    for v in ys {
        word(&mut b, v);
    }
    b
}
fn rect(x0: i32, y0: i32, x1: i32, y1: i32) -> Vec<(i32, i32, bool)> {
    // Clockwise in y-up, the TrueType outer-contour direction.
    vec![(x0, y0, true), (x0, y1, true), (x1, y1, true), (x1, y0, true)]
}
fn composite(children: &[u16]) -> Vec<u8> {
    let mut b = vec![0; 10];
    w16(&mut b, 0, 0xffff);
    for (i, child) in children.iter().enumerate() {
        word(&mut b, 3 | if i + 1 < children.len() { 0x20 } else { 0 });
        word(&mut b, *child);
        word(&mut b, 0);
        word(&mut b, 0);
    }
    b
}
fn good() -> Vec<u8> {
    glyph(&[rect(0, 0, 64, 64)])
}

fn render(bytes: &[u8], gid: u16, size: PixelSize, dst: &mut [u8]) -> Result<Placement> {
    let mut face = open_face(Memory {
        data: bytes.to_vec(),
        reads: Rc::new(Reads::default()),
    })?;
    RasterScratch::new().render(&mut face, gid, size, dst)
}
const SENTINEL: u8 = 0xa5;

/// Every Err leaves dst untouched and exposes no coverage (incl. after a success).
fn expect_no_commit(bytes: &[u8], gid: u16, size: PixelSize, dst_len: usize, expected: Error) {
    let mut face = open_face(Memory {
        data: bytes.to_vec(),
        reads: Rc::new(Reads::default()),
    })
    .unwrap();
    let mut work = RasterScratch::new();
    let mut dst = vec![SENTINEL; dst_len];
    work.render(&mut face, 0, size, &mut dst).unwrap();
    assert!(!work.coverage().is_empty());
    dst.fill(SENTINEL);
    assert_eq!(work.render(&mut face, gid, size, &mut dst), Err(expected));
    assert!(dst.iter().all(|&b| b == SENTINEL), "partial commit");
    assert!(work.coverage().is_empty(), "stale coverage exposed");
    assert_eq!(work.flattened_edges(), 0);
}

#[test]
fn pixel_square_is_exact_and_doubles() {
    // upem 1024 @16ppem: 64 units per pixel. A 3x2 px square at (1px, -1px).
    let bytes = fixture(1024, vec![good(), glyph(&[rect(64, -64, 256, 64)])]);
    let mut dst = [0u8; MAX_BITMAP_BYTES];
    let p = render(&bytes, 1, PixelSize::Px16, &mut dst).unwrap();
    assert_eq!(
        p,
        Placement { width: 3, height: 2, pitch: 1, left: 1, top: 1, advance: 16 * 64 }
    );
    assert_eq!(&dst[..2], &[0xe0, 0xe0]);
    let p = render(&bytes, 1, PixelSize::Px32, &mut dst).unwrap();
    assert_eq!(
        p,
        Placement { width: 6, height: 4, pitch: 1, left: 2, top: 2, advance: 32 * 64 }
    );
    assert_eq!(&dst[..4], &[0xfc; 4]);
    // Same square wound the other way: nonzero coverage is direction independent.
    let mut ccw = rect(64, -64, 256, 64);
    ccw.reverse();
    let bytes = fixture(1024, vec![good(), glyph(&[ccw])]);
    render(&bytes, 1, PixelSize::Px16, &mut dst).unwrap();
    assert_eq!(&dst[..2], &[0xe0, 0xe0]);
}

#[test]
fn threshold_is_half_coverage_boundary() {
    // A 1px-tall bar covering exactly x in [0, 0.5+e] of a pixel column.
    for (units, black) in [(31, false), (32, false), (33, true)] {
        // 128/255 = 0.50196 (exact integer test): 32/64 = 0.5 stays white,
        // 33/64 = 0.5156 is black.
        let bytes = fixture(1024, vec![good(), glyph(&[rect(0, 0, 64 + units, 64)])]);
        let mut dst = [0u8; MAX_BITMAP_BYTES];
        let p = render(&bytes, 1, PixelSize::Px16, &mut dst).unwrap();
        assert_eq!(p.width, 2);
        assert_eq!(dst[0], if black { 0xc0 } else { 0x80 }, "units={units}");
    }
}

#[test]
fn diagonal_coverage_is_exact_area() {
    // Triangle (0,0) (0,1px) (8px,0): one row, the hypotenuse crosses 8 columns.
    // Exact area of column c is 1 - (c + 0.5) / 8.
    let bytes = fixture(1024, vec![good(), glyph(&[vec![(0, 0, true), (0, 64, true), (512, 0, true)]])]);
    let mut face = open_face(Memory {
        data: bytes,
        reads: Rc::new(Reads::default()),
    })
    .unwrap();
    let mut work = RasterScratch::new();
    let mut dst = [0u8; MAX_BITMAP_BYTES];
    let p = work.render(&mut face, 1, PixelSize::Px16, &mut dst).unwrap();
    assert_eq!((p.width, p.height), (8, 1));
    for (c, &v) in work.coverage().iter().enumerate() {
        // Integer coverage (COVERAGE_ONE = 1 px): exact to the subpixel rounding.
        let exact = COVERAGE_ONE as f64 * (1.0 - (c as f64 + 0.5) / 8.0);
        assert!((v as f64 - exact).abs() <= 1.0, "column {c}: {v} vs {exact}");
    }
    assert_eq!(dst[0], 0xf0); // 0.9375 .. 0.5625 black, 0.4375 .. 0.0625 white
}

#[test]
fn control_box_includes_off_curve_points() {
    // Quadratic (0,0) -> (8px,0) with control (4px,4px): the curve peaks at 2px,
    // the control box (FreeType's cbox) reaches 4px.
    let bytes = fixture(
        1024,
        vec![good(), glyph(&[vec![(0, 0, true), (256, 256, false), (512, 0, true)]])],
    );
    let mut dst = [0u8; MAX_BITMAP_BYTES];
    let p = render(&bytes, 1, PixelSize::Px16, &mut dst).unwrap();
    assert_eq!((p.left, p.top, p.width, p.height), (0, 4, 8, 4));
    assert_eq!(dst[0], 0, "row above the curve's peak must stay white");
}

#[test]
fn bbox_limits() {
    for n in [MAX_BBOX_PX - 1, MAX_BBOX_PX, MAX_BBOX_PX + 1] {
        let u = (n * 64) as i32;
        for g in [glyph(&[rect(0, 0, u, 64)]), glyph(&[rect(0, 0, 64, u)])] {
            let bytes = fixture(1024, vec![good(), g]);
            if n > MAX_BBOX_PX {
                expect_no_commit(&bytes, 1, PixelSize::Px16, MAX_BITMAP_BYTES, Error::Limit);
                expect_no_commit(&bytes, 1, PixelSize::Px32, MAX_BITMAP_BYTES, Error::Limit);
            } else {
                let mut dst = vec![0; MAX_BITMAP_BYTES];
                render(&bytes, 1, PixelSize::Px16, &mut dst).unwrap();
                let mut dst = vec![0; MAX_BITMAP_BYTES];
                let p = render(&bytes, 1, PixelSize::Px32, &mut dst).unwrap();
                assert_eq!(p.width.max(p.height) as usize, 2 * n);
                assert!(p.bytes() <= MAX_BITMAP_BYTES);
            }
        }
    }
    // The largest image exactly fills MAX_BITMAP_BYTES at 32px.
    let u = (MAX_BBOX_PX * 64) as i32;
    let bytes = fixture(1024, vec![good(), glyph(&[rect(0, 0, u, u)])]);
    let mut dst = vec![0; MAX_BITMAP_BYTES];
    let p = render(&bytes, 1, PixelSize::Px32, &mut dst).unwrap();
    assert_eq!(p.bytes(), MAX_BITMAP_BYTES);
    assert!(dst.iter().all(|&b| b == 0xff));
}

#[test]
fn destination_capacity() {
    // "Allocation failure": the caller's slot is one byte short -> no commit.
    let bytes = fixture(1024, vec![good(), glyph(&[rect(0, 0, 640, 640)])]);
    let mut dst = vec![0; MAX_BITMAP_BYTES];
    let needed = render(&bytes, 1, PixelSize::Px32, &mut dst).unwrap().bytes();
    assert_eq!(needed, 20 * 3);
    expect_no_commit(&bytes, 1, PixelSize::Px32, needed - 1, Error::Limit);
    let mut exact = vec![SENTINEL; needed];
    render(&bytes, 1, PixelSize::Px32, &mut exact).unwrap();
    assert!(exact.chunks(3).all(|row| row == [0xff, 0xff, 0xf0]));
}

/// Q quadratics of exactly 3 flattened segments each, plus `lines` straight
/// edges (including the closing one): edges = 3Q + lines.
fn zigzag(quads: usize, lines: usize) -> Vec<u8> {
    assert!(lines >= 2);
    let mut c = vec![(0, 0, true)];
    for i in 0..quads as i32 {
        // |p0 - 2 p1 + p2| = 192 units = 3 px  =>  n = ceil(sqrt(6)) = 3.
        c.push((2 * i + 1, 96, false));
        c.push((2 * i + 2, 0, true));
    }
    let end = 2 * quads as i32;
    // Up, then (lines - 2) segments back along y = -64, then the closing line.
    c.push((end, -64, true));
    for k in 1..lines as i32 - 1 {
        c.push((end - end * k / (lines as i32 - 1), -64, true));
    }
    glyph(&[c])
}

#[test]
fn flattened_edge_limit() {
    let quads = (MAX_EDGES - 10) / 3;
    for total in [MAX_EDGES - 1, MAX_EDGES, MAX_EDGES + 1] {
        let lines = total - 3 * quads;
        let bytes = fixture(1024, vec![good(), zigzag(quads, lines)]);
        if total > MAX_EDGES {
            // Mid-raster failure after accumulation started: still no commit.
            expect_no_commit(&bytes, 1, PixelSize::Px16, MAX_BITMAP_BYTES, Error::Limit);
        } else {
            let mut face = open_face(Memory {
                data: bytes,
                reads: Rc::new(Reads::default()),
            })
            .unwrap();
            let mut work = RasterScratch::new();
            let mut dst = vec![0; MAX_BITMAP_BYTES];
            work.render(&mut face, 1, PixelSize::Px16, &mut dst).unwrap();
            assert_eq!(work.flattened_edges(), total);
        }
    }
}

#[test]
fn coordinate_overflow() {
    // upem 16: 1 unit = 1 px. Far from the origin but small: OK at 16px;
    // doubling the placement for 32px overflows i16 -> no commit.
    let far = glyph(&[rect(20000, 20000, 20004, 20004)]);
    let small = glyph(&[rect(0, 0, 4, 4)]);
    let bytes = fixture(16, vec![small.clone(), far]);
    let mut dst = vec![0; MAX_BITMAP_BYTES];
    let p = render(&bytes, 1, PixelSize::Px16, &mut dst).unwrap();
    assert_eq!((p.left, p.top, p.width, p.height), (20000, 20004, 4, 4));
    expect_no_commit(&bytes, 1, PixelSize::Px32, MAX_BITMAP_BYTES, Error::Bounds);
    // Each axis alone overflows when doubled.
    for g in [glyph(&[rect(20000, 0, 20004, 4)]), glyph(&[rect(0, 19996, 4, 20000)])] {
        let bytes = fixture(16, vec![small.clone(), g]);
        render(&bytes, 1, PixelSize::Px16, &mut dst).unwrap();
        expect_no_commit(&bytes, 1, PixelSize::Px32, MAX_BITMAP_BYTES, Error::Bounds);
    }
    // Extreme i16 extents: bbox limit, never a panic or a wrapped size.
    let huge = glyph(&[rect(-16000, -16000, 16000, 16000)]);
    let bytes = fixture(16, vec![small, huge]);
    expect_no_commit(&bytes, 1, PixelSize::Px16, MAX_BITMAP_BYTES, Error::Limit);
}

#[test]
fn odd_length_components_keep_even_short_loca() {
    // ttf-parser gets a Short (offset/2) mini loca: every component must start
    // on an even byte even after an odd-length sibling.
    let mut odd = glyph(&[rect(0, 0, 64, 64)]);
    odd.push(0); // trailing pad: 1 + even = odd length
    assert_eq!(odd.len() % 2, 1);
    let far = glyph(&[rect(128, 0, 192, 64)]);
    let bytes = fixture(1024, vec![good(), composite(&[2, 3]), odd, far]);
    let mut dst = [0u8; MAX_BITMAP_BYTES];
    let p = render(&bytes, 1, PixelSize::Px16, &mut dst).unwrap();
    assert_eq!((p.width, p.height, p.left, p.top), (3, 1, 0, 1));
    assert_eq!(dst[0], 0xa0);
}

#[test]
fn empty_glyph_commits_metrics_only() {
    let bytes = fixture(1024, vec![good(), vec![]]);
    let mut dst = [SENTINEL; 4];
    let p = render(&bytes, 1, PixelSize::Px32, &mut dst).unwrap();
    assert_eq!(p, Placement { advance: 32 * 64, ..Placement::default() });
    assert_eq!(dst, [SENTINEL; 4]);
}

#[test]
fn every_read_failure_has_zero_commit() {
    // Root and both components straddle read-cache slots (as in tests/outline.rs).
    let mut root = composite(&[2, 3]);
    root.resize(1024, 0);
    let mut child = glyph(&[rect(0, 0, 640, 640)]);
    child.resize(2048, 0);
    let mut last = glyph(&[rect(64, 64, 128, 128)]);
    last.resize(1024, 0);
    let bytes = fixture(1024, vec![good(), root, child, last]);
    let reads = Rc::new(Reads::default());
    let mut face = open_face(Memory {
        data: bytes.clone(),
        reads: reads.clone(),
    })
    .unwrap();
    let mut work = RasterScratch::new();
    let mut dst = vec![0; MAX_BITMAP_BYTES];
    face.trim();
    reads.count.set(0);
    work.render(&mut face, 1, PixelSize::Px32, &mut dst).unwrap();
    let count = reads.count.get();
    assert!(count >= 4, "reads={count}");
    for fail in 1..=count {
        work.render(&mut face, 0, PixelSize::Px32, &mut dst).unwrap();
        face.trim();
        reads.count.set(0);
        reads.fail.set(Some(fail));
        dst.fill(SENTINEL);
        assert_eq!(work.render(&mut face, 1, PixelSize::Px32, &mut dst), Err(Error::Io));
        assert!(dst.iter().all(|&b| b == SENTINEL), "partial commit at read {fail}");
        assert!(work.coverage().is_empty());
        reads.fail.set(None);
        face.trim();
        work.render(&mut face, 1, PixelSize::Px32, &mut dst).unwrap();
    }
    println!("every one of {count} reads: I/O failure => zero commit, retry OK");
}

#[test]
fn units_to_26_6_rounding() {
    // FreeType: scale = FT_DivFix(16 * 64, upem), value = FT_MulFix(v, scale).
    for upem in [16u16, 1000, 1024, 2048, 2000, 16384] {
        let scale = ((16i64 * 64 << 16) + upem as i64 / 2) / upem as i64;
        for v in -3000i32..=3000 {
            let m = ((v as i64).abs() * scale + 0x8000) >> 16;
            let exact = if v < 0 { -m } else { m };
            assert_eq!(units_to_26_6(v, upem, PixelSize::Px16).unwrap() as i64, exact);
            assert_eq!(units_to_26_6(v, upem, PixelSize::Px32).unwrap() as i64, 2 * exact);
        }
    }
    assert_eq!(units_to_26_6(1, 2048, PixelSize::Px16).unwrap(), 1); // 0.5 -> 1
    assert_eq!(units_to_26_6(-1, 2048, PixelSize::Px16).unwrap(), -1); // -0.5 -> -1
    assert_eq!(units_to_26_6(1, 0, PixelSize::Px16), Err(Error::Invalid));
}

#[test]
fn bitmap_box_uses_26_6_points() {
    // upem 2048 (IPAex): 1665 units = 832.5/64 px -> FT_MulFix 833 -> ceil 14 rows;
    // 1664 units = 832/64 = 13 exactly -> 13. 26.6 rounding decides the pixel box.
    for (y, top) in [(1664, 13), (1665, 14), (1663, 13)] {
        let bytes = fixture(2048, vec![glyph(&[rect(0, 0, 128, 128)]), glyph(&[rect(0, 0, 128, y)])]);
        let mut dst = [0u8; MAX_BITMAP_BYTES];
        let p = render(&bytes, 1, PixelSize::Px16, &mut dst).unwrap();
        assert_eq!(p.top, top, "y={y}");
    }
}

fn hex(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        write!(s, "{b:02x}").unwrap();
    }
    s
}

/// Dumps every corpus glyph for the Python references (EBDT/BDF dots, fontTools
/// metrics, optional FreeType, the x87 build). Asserts 32px == 2x nearest 16px.
#[test]
fn real_corpus_dump() {
    let Ok(out) = std::env::var("OS32_RASTER_DUMP") else {
        println!("SKIP real_corpus_dump: run tools/tests/test_font_raster.py");
        return;
    };
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../assets/fonts");
    let list = |name: &str| -> Vec<u32> {
        fs::read_to_string(std::env::var(name).unwrap())
            .unwrap()
            .lines()
            .map(|s| s.parse().unwrap())
            .collect()
    };
    let (ja, latin, jis) = (list("OS32_FONT_JA"), list("OS32_FONT_LATIN"), list("OS32_FONT_JIS"));
    let fonts = [
        ("shinonome", PathBuf::from(std::env::var("OS32_FONT_SHINONOME").unwrap()), &ja, true),
        ("inconsolata", root.join("Inconsolata-Regular.ttf"), &latin, true),
        ("ipaexg", root.join("ipaexg.ttf"), &jis, true),
        ("ipaexm", root.join("ipaexm.ttf"), &jis, false),
    ];
    let mut text = String::new();
    let mut work = RasterScratch::new();
    let mut b16 = vec![0u8; MAX_BITMAP_BYTES];
    let mut b32 = vec![0u8; MAX_BITMAP_BYTES];
    for (name, path, corpus, coverage) in fonts {
        let file = File::open(&path).unwrap();
        let size = file.metadata().unwrap().len();
        let mut face = open_face(Disk(file, size)).unwrap();
        let m = face.metrics();
        let asc = units_to_26_6(m.ascender.into(), m.units_per_em, PixelSize::Px16).unwrap();
        let desc = units_to_26_6(m.descender.into(), m.units_per_em, PixelSize::Px16).unwrap();
        writeln!(text, "F {name} {} {asc} {desc}", m.units_per_em).unwrap();
        let (mut w, mut h, mut edges, mut bytes, mut fallback) = (0, 0, 0, 0, 0);
        for &cp in corpus.iter() {
            let gid = face.glyph_index(cp).unwrap().unwrap();
            let p = match work.render(&mut face, gid, PixelSize::Px16, &mut b16) {
                Ok(p) => p,
                Err(e) => {
                    fallback += 1;
                    writeln!(text, "E {name} {cp} {gid} {e:?}").unwrap();
                    continue;
                }
            };
            w = w.max(p.width);
            h = h.max(p.height);
            edges = edges.max(work.flattened_edges());
            let cov = if coverage && !work.coverage().is_empty() {
                // 8-bit like FreeType's gray output: min(coverage, 255).
                let q: Vec<u8> = work.coverage().iter().map(|&c| c.min(255) as u8).collect();
                hex(&q)
            } else {
                String::from("-")
            };
            let q = work.render(&mut face, gid, PixelSize::Px32, &mut b32).unwrap();
            bytes = bytes.max(q.bytes());
            assert_eq!(
                (q.width, q.height, q.left, q.top, q.advance),
                (2 * p.width, 2 * p.height, 2 * p.left, 2 * p.top, 2 * p.advance)
            );
            for y in 0..q.height as usize {
                for x in 0..q.width as usize {
                    let a = b32[y * q.pitch as usize + x / 8] >> (7 - x % 8) & 1;
                    let s = b16[(y / 2) * p.pitch as usize + (x / 2) / 8] >> (7 - (x / 2) % 8) & 1;
                    assert_eq!(a, s, "{name} U+{cp:04X} 32px is not 2x nearest");
                }
            }
            writeln!(
                text,
                "G {name} {cp} {gid} {} {} {} {} {} {} {} {}",
                p.width,
                p.height,
                p.pitch,
                p.left,
                p.top,
                p.advance,
                if p.bytes() == 0 { String::from("-") } else { hex(&b16[..p.bytes()]) },
                cov
            )
            .unwrap();
        }
        assert_eq!(fallback, 0, "{name}: unexpected KCG/replacement fallback");
        println!(
            "RASTER {name} count={} fallback=0 max16 {w}x{h} flattened_edges={edges} max32_bytes={bytes} 32px==2x16px",
            corpus.len()
        );
    }
    println!(
        "RasterScratch bytes={} (GlyphScratch inside) limit={} MAX_BITMAP_BYTES={MAX_BITMAP_BYTES}",
        std::mem::size_of::<RasterScratch>(),
        MAX_GLYPH_WORK_BYTES
    );
    fs::write(out, text).unwrap();
}
