//! Whole-file Face is only the oracle; production reads through pread and a3 LRU.
use os32_font::{
    limits::*,
    outline::{GlyphScratch, Outline, Usage},
    reader::{Error, Face, Result, Source},
};
use std::{
    cell::Cell,
    collections::BTreeMap,
    fs::{self, File},
    os::unix::fs::FileExt,
    path::PathBuf,
    rc::Rc,
};
use ttf_parser::OutlineBuilder;

#[derive(Default)]
struct Reads {
    count: Cell<usize>,
    fail: Cell<Option<usize>>,
    io: Cell<bool>,
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
        dst.copy_from_slice(&self.data[at as usize..at as usize + dst.len()]);
        if self.reads.fail.get() == Some(self.reads.count.get()) {
            return if self.reads.io.get() {
                Err(Error::Io)
            } else {
                Ok(dst.len() - 1)
            };
        }
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
fn memory(data: Vec<u8>) -> Memory {
    Memory {
        data,
        reads: Rc::new(Reads::default()),
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

// Valid synthetic SFNT, real reader and public parser, no parser stand-in.
fn fixture(glyphs: Vec<Vec<u8>>) -> Vec<u8> {
    let n = glyphs.len() as u16;
    let mut parts = BTreeMap::new();
    let mut head = vec![0; 54];
    w32(&mut head, 0, 0x10000);
    w32(&mut head, 12, 0x5f0f3cf5);
    w16(&mut head, 18, 1024);
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
    for (t, d) in [
        (*b"head", head),
        (*b"hhea", hhea),
        (*b"maxp", maxp),
        (*b"cmap", cmap),
        (*b"glyf", glyf),
        (*b"loca", loca),
        (*b"hmtx", vec![0; 4 + (n as usize - 1) * 2]),
    ] {
        parts.insert(t, d);
    }
    let mut b = vec![0; 12 + parts.len() * 16];
    w32(&mut b, 0, 0x10000);
    w16(&mut b, 4, parts.len() as u16);
    for (i, (t, d)) in parts.into_iter().enumerate() {
        // Isolate glyph I/O from the directory/scalars, including fault after a
        // successful root read and before a component's bytes have arrived.
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
fn simple(points: usize, contours: usize) -> Vec<u8> {
    assert!(points >= contours * 2);
    let mut b = vec![0; 10];
    w16(&mut b, 0, contours as u16);
    for i in 0..contours {
        word(&mut b, ((i + 1) * points / contours - 1) as u16);
    }
    word(&mut b, 0); // no hint instructions
                     // All on-curve, short positive x/y deltas: nonzero bounds, exactly n edges.
    b.extend(vec![0x37; points]);
    b.extend(vec![1; points * 2]);
    b
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
fn expect(glyphs: Vec<Vec<u8>>, expected: core::result::Result<(), Error>) {
    let mut face = Face::open(memory(fixture(glyphs))).unwrap();
    let mut work = GlyphScratch::new();
    work.outline(&mut face, 0).unwrap(); // prior committed outline must also disappear
    let result = work.outline(&mut face, 1).map(|_| ());
    assert_eq!(result, expected);
    if result.is_err() {
        assert_eq!(work.committed_counts(), (0, 0, 0), "partial commit");
    }
}
fn good() -> Vec<u8> {
    simple(4, 1)
}

#[test]
fn simple_and_composite_limits() {
    for points in [MAX_POINTS - 1, MAX_POINTS, MAX_POINTS + 1] {
        expect(
            vec![good(), simple(points, 1)],
            if points > MAX_POINTS {
                Err(Error::Limit)
            } else {
                Ok(())
            },
        );
    }
    for contours in [MAX_CONTOURS - 1, MAX_CONTOURS, MAX_CONTOURS + 1] {
        expect(
            vec![good(), simple(contours * 2, contours)],
            if contours > MAX_CONTOURS {
                Err(Error::Limit)
            } else {
                Ok(())
            },
        );
    }
    for count in [MAX_COMPONENTS - 1, MAX_COMPONENTS, MAX_COMPONENTS + 1] {
        expect(
            vec![good(), composite(&vec![2; count]), good()],
            if count > MAX_COMPONENTS {
                Err(Error::Limit)
            } else {
                Ok(())
            },
        );
    }
    for depth in [
        MAX_COMPONENT_DEPTH - 1,
        MAX_COMPONENT_DEPTH,
        MAX_COMPONENT_DEPTH + 1,
    ] {
        let mut glyphs = vec![good()];
        for i in 0..depth {
            glyphs.push(composite(&[(i + 2) as u16]));
        }
        glyphs.push(good());
        expect(
            glyphs,
            if depth > MAX_COMPONENT_DEPTH {
                Err(Error::Limit)
            } else {
                Ok(())
            },
        );
    }
    // Repeated components count expanded points/contours (not unique glyphs).
    expect(
        vec![good(), composite(&[2, 2]), simple(MAX_POINTS / 2 + 1, 1)],
        Err(Error::Limit),
    );
    expect(
        vec![
            good(),
            composite(&[2, 2]),
            simple(MAX_CONTOURS + 2, MAX_CONTOURS / 2 + 1),
        ],
        Err(Error::Limit),
    );
}

#[test]
fn byte_limits() {
    for span in [
        MAX_GLYPH_SPAN_BYTES - 1,
        MAX_GLYPH_SPAN_BYTES,
        MAX_GLYPH_SPAN_BYTES + 1,
    ] {
        let mut g = good();
        g.resize(span, 0);
        expect(
            vec![good(), g],
            if span > MAX_GLYPH_SPAN_BYTES {
                Err(Error::Limit)
            } else {
                Ok(())
            },
        );
    }
    for total in [
        MAX_TOTAL_INPUT_BYTES - 1,
        MAX_TOTAL_INPUT_BYTES,
        MAX_TOTAL_INPUT_BYTES + 1,
    ] {
        let mut root = composite(&[2, 3]);
        root.resize(1024, 0);
        let mut child = good();
        child.resize(1024, 0);
        let mut last = good();
        last.resize(total - 2048, 0);
        expect(
            vec![good(), root, child, last],
            if total > MAX_TOTAL_INPUT_BYTES {
                Err(Error::Limit)
            } else {
                Ok(())
            },
        );
    }
    // Repeats consume input budget each time, even when gid is the same.
    let mut child = good();
    child.resize(1600, 0);
    expect(
        vec![good(), composite(&[2, 2, 2]), child],
        Err(Error::Limit),
    );
}

#[test]
fn cycles_and_runs() {
    expect(vec![good(), composite(&[1])], Err(Error::Invalid));
    expect(
        vec![good(), composite(&[2]), composite(&[1])],
        Err(Error::Invalid),
    );
    for repeats in [3, 4, 5] {
        let mut g = good();
        g.truncate(14);
        g.extend([0x3f, (repeats - 1) as u8]);
        g.extend(vec![1; repeats * 2]);
        expect(
            vec![good(), g],
            if repeats == 4 {
                Ok(())
            } else {
                Err(Error::Invalid)
            },
        );
    }
    let mut g = good();
    g.truncate(g.len() - 1);
    expect(vec![good(), g], Err(Error::Invalid));
    let mut g = simple(4, 2);
    w16(&mut g, 14, 1); // instructions claim too many bytes below
    w16(&mut g, 10, 3);
    w16(&mut g, 12, 2);
    expect(vec![good(), g], Err(Error::Invalid));
    let mut g = composite(&[2]);
    w16(&mut g, 10, 0x103);
    word(&mut g, 40);
    expect(vec![good(), g, good()], Err(Error::Invalid));
    let mut g = composite(&[2]);
    g.truncate(17);
    expect(vec![good(), g, good()], Err(Error::Invalid));
    expect(vec![good(), composite(&[99])], Err(Error::Bounds));
}

#[test]
fn unsupported_and_empty() {
    for flags in [1, 0x0803] {
        let mut g = composite(&[2]);
        w16(&mut g, 10, flags);
        expect(vec![good(), g, good()], Err(Error::Unsupported));
    }
    expect(vec![good(), vec![]], Ok(()));
    expect(vec![good(), vec![0; 10]], Ok(()));
    expect(vec![good(), composite(&[2]), vec![]], Ok(()));
}

#[test]
fn every_mid_read_failure_has_zero_commit() {
    let mut root = composite(&[2, 3]);
    root.resize(1024, 0);
    let mut child = good();
    child.resize(2048, 0);
    let mut last = good();
    last.resize(1024, 0);
    let bytes = fixture(vec![good(), root, child, last]);
    let source = memory(bytes.clone());
    let reads = source.reads.clone();
    let mut face = Face::open(source).unwrap();
    face.trim();
    reads.count.set(0);
    let mut work = GlyphScratch::new();
    work.outline(&mut face, 1).unwrap();
    let count = reads.count.get();
    assert!(count >= 4);
    for io in [false, true] {
        for fail in 1..=count {
            let source = memory(bytes.clone());
            let reads = source.reads.clone();
            let mut face = Face::open(source).unwrap();
            work.outline(&mut face, 0).unwrap();
            assert_ne!(work.committed_counts(), (0, 0, 0));
            face.trim();
            reads.count.set(0);
            reads.fail.set(Some(fail));
            reads.io.set(io);
            assert_eq!(
                work.outline(&mut face, 1).err(),
                Some(if io { Error::Io } else { Error::ShortRead }),
                "I/O {fail}/{count}"
            );
            assert_eq!(work.committed_counts(), (0, 0, 0));
            reads.fail.set(None);
            face.trim();
            work.outline(&mut face, 1).unwrap();
        }
    }
    println!(
        "every one of {count} read boundaries: short and I/O failure => zero commit, retry OK"
    );
}

#[derive(Debug, PartialEq)]
enum Command {
    Move(f32, f32),
    Line(f32, f32),
    Quad(f32, f32, f32, f32),
    Close,
}
#[derive(Default)]
struct Oracle(Vec<Command>);
impl OutlineBuilder for Oracle {
    fn move_to(&mut self, x: f32, y: f32) {
        self.0.push(Command::Move(x, y));
    }
    fn line_to(&mut self, x: f32, y: f32) {
        self.0.push(Command::Line(x, y));
    }
    fn quad_to(&mut self, x1: f32, y1: f32, x: f32, y: f32) {
        self.0.push(Command::Quad(x1, y1, x, y));
    }
    fn curve_to(&mut self, _: f32, _: f32, _: f32, _: f32, _: f32, _: f32) {
        panic!("glyf emitted cubic");
    }
    fn close(&mut self) {
        self.0.push(Command::Close);
    }
}
fn commands(out: &Outline<'_>) -> Vec<Command> {
    let mut result = Vec::new();
    for c in out.contours {
        let edges = &out.edges[c.first_edge as usize..c.end_edge as usize];
        if let Some(first) = edges.first() {
            let p = out.points[first.from as usize];
            result.push(Command::Move(p.x, p.y));
        }
        for e in edges {
            let p = out.points[e.to as usize];
            result.push(match e.control {
                Some(i) => {
                    let q = out.points[i as usize];
                    Command::Quad(q.x, q.y, p.x, p.y)
                }
                None => Command::Line(p.x, p.y),
            });
        }
        result.push(Command::Close);
    }
    result
}
#[test]
fn real_corpus_matches_whole_file_face() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../assets/fonts");
    let jis = fs::read_to_string(std::env::var("OS32_FONT_JIS").expect("run test_font_outline.py"))
        .unwrap();
    let jis: Vec<u32> = jis.lines().map(|s| s.parse().unwrap()).collect();
    assert_eq!(jis.len(), 6879);
    let latin: Vec<u32> = (0x20..=0x7e).chain(0xa0..=0xff).collect();
    let mut ja = jis.clone();
    ja.extend(&latin);
    ja.sort();
    ja.dedup();
    let paths = [
        (
            PathBuf::from(std::env::var("OS32_FONT_SHINONOME").unwrap()),
            &ja,
        ),
        (root.join("Inconsolata-Regular.ttf"), &latin),
        (root.join("ipaexg.ttf"), &jis),
        (root.join("ipaexm.ttf"), &jis),
    ];
    let mut work = GlyphScratch::new();
    for (path, corpus) in paths {
        let bytes = fs::read(&path).unwrap();
        let oracle = ttf_parser::Face::parse(&bytes, 0).unwrap();
        let file = File::open(&path).unwrap();
        let size = file.metadata().unwrap().len();
        let mut face = Face::open(Disk(file, size)).unwrap();
        let mut max = Usage::default();
        let mut max_edges = 0;
        let mut max_points = 0;
        let mut unsupported = Vec::new();
        for &cp in corpus {
            let gid = face.glyph_index(cp).unwrap().unwrap();
            assert_eq!(
                Some(ttf_parser::GlyphId(gid)),
                oracle.glyph_index(char::from_u32(cp).unwrap())
            );
            let mut reference = Oracle::default();
            let bbox = oracle.outline_glyph(ttf_parser::GlyphId(gid), &mut reference);
            match work.outline(&mut face, gid) {
                Ok(out) => {
                    assert_eq!(commands(&out), reference.0, "{path:?} U+{cp:04X}");
                    assert_eq!(out.bounds, bbox, "U+{cp:04X}");
                    max.input_bytes = max.input_bytes.max(out.usage.input_bytes);
                    max.points = max.points.max(out.usage.points);
                    max.contours = max.contours.max(out.usage.contours);
                    max.components = max.components.max(out.usage.components);
                    max.depth = max.depth.max(out.usage.depth);
                    max.glyphs = max.glyphs.max(out.usage.glyphs);
                    max_edges = max_edges.max(out.edges.len());
                    max_points = max_points.max(out.points.len());
                }
                Err(e) => unsupported.push((cp, e)),
            }
        }
        // a1 + Shinonome investigation: exact unsupported/exceeded list is empty.
        assert_eq!(unsupported, vec![], "{path:?}: unexpected fallback list");
        println!(
            "CORPUS {} count={} unsupported=0 {max:?} outline_points={max_points} curve_edges={max_edges}",
            path.file_name().unwrap().to_string_lossy(),
            corpus.len()
        );
    }
    println!(
        "GlyphScratch bytes={} dedicated limit={} read cache separately={} heap=0 extra FD=0",
        std::mem::size_of::<GlyphScratch>(),
        MAX_GLYPH_WORK_BYTES,
        READ_CACHE_SLOTS * READ_CACHE_SLOT_BYTES
    );
}

// Run separately with MAX_EDGES=128 so raw-point and edge guards are independently
// reachable; the default 512 edge bound is otherwise implied by MAX_POINTS.
#[test]
fn edge_limit_independent() {
    if MAX_EDGES >= MAX_POINTS {
        return;
    }
    for n in [MAX_EDGES - 1, MAX_EDGES, MAX_EDGES + 1] {
        expect(
            vec![good(), simple(n, 1)],
            if n > MAX_EDGES {
                Err(Error::Limit)
            } else {
                Ok(())
            },
        );
    }
}

#[test]
fn implicit_point_capacity() {
    // All off-curve points exercise the tight derived bound: two command points
    // per stored point, plus one move per contour. No heap in the product path.
    let mut g = simple(MAX_POINTS, MAX_CONTOURS);
    let flags = 12 + MAX_CONTOURS * 2;
    g[flags..flags + MAX_POINTS].fill(0x36);
    let mut face = Face::open(memory(fixture(vec![good(), g]))).unwrap();
    let mut work = GlyphScratch::new();
    let required = 2 * MAX_POINTS + MAX_CONTOURS;
    if MAX_OUTLINE_POINTS < required {
        assert_eq!(work.outline(&mut face, 1).err(), Some(Error::Limit));
        assert_eq!(work.committed_counts(), (0, 0, 0));
    } else {
        let result = work.outline(&mut face, 1);
        assert!(result.is_ok(), "exact implicit-point capacity rejected");
        let out = result.unwrap();
        assert_eq!(out.points.len(), required);
        assert_eq!(out.edges.len(), MAX_POINTS);
        assert_eq!(out.contours.len(), MAX_CONTOURS);
    }
}

#[test]
fn empty_contour_keeps_its_own_edge_range() {
    // The final off-curve point makes ttf-parser emit close without move.
    // It must not inherit the preceding contour's edges.
    let mut g = vec![0; 10];
    w16(&mut g, 0, 2);
    word(&mut g, 1);
    word(&mut g, 2);
    word(&mut g, 0);
    g.extend([0x37, 0x37, 0x36]);
    g.extend([1; 6]);
    let bytes = fixture(vec![good(), g]);
    let reference = ttf_parser::Face::parse(&bytes, 0).unwrap();
    let mut oracle = Oracle::default();
    let bbox = reference.outline_glyph(ttf_parser::GlyphId(1), &mut oracle);
    let mut face = Face::open(memory(bytes.clone())).unwrap();
    let mut work = GlyphScratch::new();
    let out = work.outline(&mut face, 1).unwrap();
    assert_eq!(out.contours.len(), 2);
    assert_eq!(out.contours[1].first_edge, out.contours[1].end_edge);
    assert_eq!(commands(&out), oracle.0);
    assert_eq!(out.bounds, bbox);
}
