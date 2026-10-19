//! Whole-file ttf-parser is a host oracle only; the product Source uses pread.
use os32_font::{
    limits::*,
    reader::{read_exact_at, Error, Face, Result, Source},
};
use std::{
    cell::Cell,
    collections::BTreeMap,
    fs::{self, File},
    os::unix::fs::FileExt,
    path::PathBuf,
    rc::Rc,
};
use ttf_parser::{GlyphId, Tag};

#[derive(Default)]
struct Stats {
    reads: Cell<usize>,
    max: Cell<usize>,
    fail: Cell<Option<usize>>,
    short: Cell<bool>,
}
struct Disk {
    file: File,
    size: u64,
    stats: Rc<Stats>,
}
impl Source for Disk {
    fn file_size(&self) -> u64 {
        self.size
    }
    fn read_at(&mut self, offset: u64, dst: &mut [u8]) -> Result<usize> {
        self.stats.reads.set(self.stats.reads.get() + 1);
        self.stats.max.set(self.stats.max.get().max(dst.len()));
        let n = self.file.read_at(dst, offset).map_err(|_| Error::Io)?;
        if self.stats.fail.get() == Some(self.stats.reads.get()) || self.stats.short.get() {
            return Ok(n.saturating_sub(1));
        }
        Ok(n)
    }
}
fn disk(path: &PathBuf) -> (Disk, Rc<Stats>) {
    let file = File::open(path).unwrap();
    let size = file.metadata().unwrap().len();
    let stats = Rc::new(Stats::default());
    (
        Disk {
            file,
            size,
            stats: stats.clone(),
        },
        stats,
    )
}
#[derive(Clone)]
struct Memory {
    data: Vec<u8>,
    size: u64,
    stats: Rc<Stats>,
}
impl Source for Memory {
    fn file_size(&self) -> u64 {
        self.size
    }
    fn read_at(&mut self, offset: u64, dst: &mut [u8]) -> Result<usize> {
        self.stats.reads.set(self.stats.reads.get() + 1);
        self.stats.max.set(self.stats.max.get().max(dst.len()));
        if offset > self.size || dst.len() as u64 > self.size - offset {
            return Err(Error::Io);
        }
        let offset = usize::try_from(offset).map_err(|_| Error::Io)?;
        dst.fill(0);
        if let Some(data) = self.data.get(offset..) {
            let n = data.len().min(dst.len());
            dst[..n].copy_from_slice(&data[..n]);
        }
        Ok(if self.stats.short.get() {
            dst.len().saturating_sub(1)
        } else {
            dst.len()
        })
    }
}
fn memory(data: Vec<u8>) -> Memory {
    Memory {
        size: data.len() as u64,
        data,
        stats: Rc::new(Stats::default()),
    }
}
fn w16(b: &mut [u8], at: usize, n: u16) {
    b[at..at + 2].copy_from_slice(&n.to_be_bytes());
}
fn w32(b: &mut [u8], at: usize, n: u32) {
    b[at..at + 4].copy_from_slice(&n.to_be_bytes());
}
fn r16(b: &[u8], at: usize) -> u16 {
    u16::from_be_bytes(b[at..at + 2].try_into().unwrap())
}
fn r32(b: &[u8], at: usize) -> u32 {
    u32::from_be_bytes(b[at..at + 4].try_into().unwrap())
}
fn record(b: &[u8], tag: &[u8; 4]) -> usize {
    (0..r16(b, 4) as usize)
        .map(|i| 12 + i * 16)
        .find(|&i| &b[i..i + 4] == tag)
        .unwrap()
}
fn table(b: &[u8], tag: &[u8; 4]) -> usize {
    r32(b, record(b, tag) + 8) as usize
}
fn subtable(b: &[u8]) -> usize {
    let c = table(b, b"cmap");
    c + r32(b, c + 8) as usize
}

fn cmap4(segments: usize, array: bool) -> Vec<u8> {
    assert!(segments >= 2);
    let len = 16 + 8 * segments + if array { 2 } else { 0 };
    let mut b = vec![0; len];
    w16(&mut b, 0, 4);
    w16(&mut b, 2, u16::try_from(len).unwrap_or(u16::MAX));
    w16(&mut b, 6, (segments * 2) as u16);
    for i in 0..segments {
        let cp = if i == segments - 1 { 0xffff } else { i as u16 };
        w16(&mut b, 14 + i * 2, cp);
        w16(&mut b, 16 + segments * 2 + i * 2, cp);
        w16(&mut b, 16 + segments * 4 + i * 2, 1u16.wrapping_sub(cp));
    }
    if array {
        w16(&mut b, 16 + segments * 4, 0);
        w16(&mut b, 16 + segments * 6, (segments * 2) as u16);
        w16(&mut b, 16 + segments * 8, 2);
    }
    b
}
fn cmap12(groups: usize) -> Vec<u8> {
    let mut b = vec![0; 16 + groups * 12];
    let len = b.len() as u32;
    w16(&mut b, 0, 12);
    w32(&mut b, 4, len);
    w32(&mut b, 12, groups as u32);
    for i in 0..groups {
        w32(&mut b, 16 + i * 12, (i * 2) as u32);
        w32(&mut b, 20 + i * 12, (i * 2) as u32);
        w32(&mut b, 24 + i * 12, 1);
    }
    b
}
fn fixture(sub: Vec<u8>, long: bool, table_count: usize, records: usize) -> Vec<u8> {
    let mut parts = BTreeMap::new();
    let mut head = vec![0; 54];
    w32(&mut head, 0, 0x10000);
    w32(&mut head, 12, 0x5f0f3cf5);
    w16(&mut head, 18, 1000);
    w16(&mut head, 50, u16::from(long));
    let mut hhea = vec![0; 36];
    w32(&mut hhea, 0, 0x10000);
    w16(&mut hhea, 4, 800);
    w16(&mut hhea, 6, (-200i16) as u16);
    w16(&mut hhea, 34, 2);
    let mut maxp = vec![0; 32];
    w32(&mut maxp, 0, 0x10000);
    w16(&mut maxp, 4, 4);
    let mut hmtx = vec![0; 12];
    w16(&mut hmtx, 0, 400);
    w16(&mut hmtx, 4, 600);
    let mut loca = vec![0; 5 * if long { 4 } else { 2 }];
    for (i, v) in [0, 0, 4, 8, 12].iter().enumerate() {
        if long {
            w32(&mut loca, i * 4, *v)
        } else {
            w16(&mut loca, i * 2, (*v / 2) as u16)
        }
    }
    let mut cmap = vec![0; 4 + records * 8];
    w16(&mut cmap, 2, records as u16);
    let offset = cmap.len() as u32;
    for i in 0..records {
        w16(&mut cmap, 4 + i * 8, 3);
        w16(
            &mut cmap,
            6 + i * 8,
            if r16(&sub, 0) == 12 { 10 } else { 1 },
        );
        w32(&mut cmap, 8 + i * 8, offset);
    }
    cmap.extend(sub);
    for (tag, data) in [
        (*b"cmap", cmap),
        (*b"head", head),
        (*b"hhea", hhea),
        (*b"maxp", maxp),
        (*b"hmtx", hmtx),
        (*b"loca", loca),
        (*b"glyf", vec![0; 12]),
    ] {
        parts.insert(tag, data);
    }
    for i in parts.len()..table_count {
        parts.insert(
            [b'z', b'z', b'0' + (i / 10) as u8, b'0' + (i % 10) as u8],
            vec![0; 4],
        );
    }
    let mut b = vec![0; 12 + parts.len() * 16];
    w32(&mut b, 0, 0x10000);
    w16(&mut b, 4, parts.len() as u16);
    for (i, (tag, data)) in parts.into_iter().enumerate() {
        while b.len() % 4 != 0 {
            b.push(0);
        }
        let offset = b.len() as u32;
        b[12 + i * 16..16 + i * 16].copy_from_slice(&tag);
        w32(&mut b, 20 + i * 16, offset);
        w32(&mut b, 24 + i * 16, data.len() as u32);
        b.extend(data);
    }
    b
}
fn small() -> Vec<u8> {
    fixture(cmap4(2, true), false, 7, 1)
}
fn rejected(b: Vec<u8>) {
    assert!(Face::open(memory(b)).is_err(), "corrupt face published");
}

#[test]
fn read_at_contract() {
    let mut m = memory(vec![0; 16]);
    let mut b = [0; 1];
    assert_eq!(read_exact_at(&mut m, 17, &mut []), Err(Error::Bounds));
    assert_eq!(read_exact_at(&mut m, u64::MAX, &mut b), Err(Error::Bounds));
    assert_eq!(read_exact_at(&mut m, 16, &mut b), Err(Error::Bounds));
    assert_eq!(m.stats.reads.get(), 0, "bounds checked before source");
    assert_eq!(read_exact_at(&mut m, 16, &mut []), Ok(()));
    assert_eq!(m.stats.reads.get(), 0);
    m.stats.short.set(true);
    assert_eq!(read_exact_at(&mut m, 0, &mut b), Err(Error::ShortRead));
    let mut overcount = Overcount;
    assert_eq!(
        read_exact_at(&mut overcount, 0, &mut b),
        Err(Error::ShortRead)
    );
    struct Overcount;
    impl Source for Overcount {
        fn file_size(&self) -> u64 {
            16
        }
        fn read_at(&mut self, _: u64, d: &mut [u8]) -> Result<usize> {
            Ok(d.len() + 1)
        }
    }
}

#[test]
fn offset_length_overflow() {
    for (offset, length) in [(u32::MAX, 2), (u32::MAX - 1, 4), (0x100, u32::MAX)] {
        let mut b = small();
        let r = record(&b, b"glyf");
        w32(&mut b, r + 8, offset);
        w32(&mut b, r + 12, length);
        // A synthetic large size ensures u32 table-end overflow is rejected even
        // when the source advertises a bigger u64 file. No huge allocation.
        let mut m = memory(b);
        m.size = u64::MAX;
        assert!(Face::open(m).is_err(), "overflow face published");
    }
    let mut b = small();
    let r = record(&b, b"glyf");
    w32(&mut b, r + 12, 0x10000);
    rejected(b);
    let mut b = small();
    let c = table(&b, b"cmap");
    w32(&mut b, c + 8, u32::MAX);
    rejected(b);
}
#[test]
fn reversed_order() {
    let mut b = small();
    let first = b[12..28].to_vec();
    let second = b[28..44].to_vec();
    b[12..28].copy_from_slice(&second);
    b[28..44].copy_from_slice(&first);
    rejected(b);
    let mut b = fixture(cmap4(3, false), false, 7, 1);
    let s = subtable(&b);
    w16(&mut b, s + 16, 0);
    rejected(b);
    let mut b = fixture(cmap12(2), true, 7, 1);
    let s = subtable(&b);
    w32(&mut b, s + 28, 0);
    w32(&mut b, s + 32, 0);
    rejected(b);
    for long in [false, true] {
        let mut b = fixture(cmap4(2, false), long, 7, 1);
        let l = table(&b, b"loca");
        if long {
            w32(&mut b, l + 4, 6)
        } else {
            w16(&mut b, l + 2, 3)
        }
        let mut f = Face::open(memory(b)).unwrap();
        assert_eq!(
            f.glyph_range(1),
            Err(Error::Invalid),
            "reversed loca published"
        );
    }
}
#[test]
fn id_range_offset_boundaries() {
    let good = small();
    let mut f = Face::open(memory(good.clone())).unwrap();
    assert_eq!(f.glyph_index(0), Ok(Some(2))); // last complete u16 in subtable
    for delta in [-1i32, 1, 2] {
        let mut b = good.clone();
        let s = subtable(&b);
        w16(&mut b, s + 28, (4 + delta) as u16);
        rejected(b);
    }
    let mut b = good.clone();
    let s = subtable(&b);
    w16(&mut b, s + 28, 2);
    rejected(b); // before glyphIdArray
    let mut b = good;
    let s = subtable(&b);
    w16(&mut b, s + 32, 0);
    let mut f = Face::open(memory(b)).unwrap();
    assert_eq!(f.glyph_index(0), Ok(None));
}
#[test]
fn scalar_and_table_bounds() {
    for (tag, offset, value) in [
        (b"head", 18, 0),
        (b"head", 50, 2),
        (b"maxp", 4, 0),
        (b"hhea", 34, 0),
        (b"hhea", 34, 5),
    ] {
        let mut b = small();
        let t = table(&b, tag);
        w16(&mut b, t + offset, value);
        rejected(b);
    }
    for tag in [b"head", b"hhea", b"maxp", b"cmap", b"loca", b"hmtx"] {
        let mut b = small();
        let t = record(&b, tag);
        w32(&mut b, t + 12, 1);
        rejected(b);
    }
    let mut f = Face::open(memory(small())).unwrap();
    assert_eq!(f.glyph_range(4), Err(Error::Bounds));
    assert_eq!(f.advance(4), Err(Error::Bounds));
    assert!(f.glyph_range(0).unwrap().is_empty());
    assert_eq!(
        (f.advance(0), f.advance(1), f.advance(2), f.advance(3)),
        (Ok(400), Ok(600), Ok(600), Ok(600))
    );
    for long in [false, true] {
        let mut b = fixture(cmap4(2, false), long, 7, 1);
        let l = table(&b, b"loca");
        if long {
            w32(&mut b, l + 16, 100)
        } else {
            w16(&mut b, l + 8, 50)
        }
        assert_eq!(
            Face::open(memory(b)).unwrap().glyph_range(3),
            Err(Error::Bounds)
        );
    }
}
#[test]
fn unsupported_and_cmap_bounds() {
    let mut b = small();
    w32(&mut b, 0, u32::from_be_bytes(*b"OTTO"));
    assert!(matches!(Face::open(memory(b)), Err(Error::Unsupported)));
    let mut b = small();
    let r = record(&b, b"glyf");
    b[r..r + 4].copy_from_slice(b"fvar"); // remains sorted before head
    assert!(matches!(Face::open(memory(b)), Err(Error::Unsupported)));
    let mut b = small();
    let c = table(&b, b"cmap");
    w16(&mut b, c + 4, 99);
    assert!(matches!(Face::open(memory(b)), Err(Error::Unsupported)));
    for (offset, value) in [
        (4, u32::MAX),
        (12, u32::MAX),
        (16, 2),
        (20, 0x110000),
        (24, u32::MAX),
    ] {
        let mut b = fixture(cmap12(1), true, 7, 1);
        let s = subtable(&b);
        w32(&mut b, s + offset, value);
        rejected(b);
    }
    let mut b = small();
    let s = subtable(&b);
    w16(&mut b, s + 20, 3); // startCode > endCode
    rejected(b);
}

#[test]
fn exact_and_exceeded_limits() {
    for tables in [MAX_TABLES - 1, MAX_TABLES, MAX_TABLES + 1] {
        let f = Face::open(memory(fixture(cmap4(2, false), false, tables, 1)));
        assert_eq!(f.is_ok(), tables <= MAX_TABLES, "table cap {tables}");
    }
    for records in [
        MAX_CMAP_SUBTABLES - 1,
        MAX_CMAP_SUBTABLES,
        MAX_CMAP_SUBTABLES + 1,
    ] {
        let f = Face::open(memory(fixture(cmap4(2, false), false, 7, records)));
        assert_eq!(
            f.is_ok(),
            records <= MAX_CMAP_SUBTABLES,
            "record cap {records}"
        );
    }
    // format4's u16 byte length fits at most 8189 segments with no glyphIdArray.
    // The a1 segment cap 8192 is above that physical format limit; reject the
    // impossible length rather than pretending such a table is a valid fixture.
    for count in [
        8189,
        MAX_CMAP_SEGMENTS - 1,
        MAX_CMAP_SEGMENTS,
        MAX_CMAP_SEGMENTS + 1,
    ] {
        let f = Face::open(memory(fixture(cmap4(count, false), false, 7, 1)));
        assert_eq!(
            f.is_ok(),
            count <= 8189,
            "segment/format length cap {count}"
        );
    }
    for groups in [MAX_CMAP_GROUPS - 1, MAX_CMAP_GROUPS, MAX_CMAP_GROUPS + 1] {
        let f = Face::open(memory(fixture(cmap12(groups), true, 7, 1)));
        assert_eq!(f.is_ok(), groups <= MAX_CMAP_GROUPS, "group cap {groups}");
    }
    assert!(READ_CACHE_SLOTS > 0);
    assert_eq!(READ_CACHE_SLOT_BYTES, 1024);
    println!(
        "sizeof host Face<Memory>={} (cache data={}, no allocation in reader)",
        std::mem::size_of::<Face<Memory>>(),
        READ_CACHE_SLOTS * READ_CACHE_SLOT_BYTES
    );
}

fn fonts() -> Vec<PathBuf> {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../assets/fonts");
    ["Inconsolata-Regular.ttf", "ipaexg.ttf", "ipaexm.ttf"]
        .iter()
        .map(|p| root.join(p))
        .collect()
}
fn compare(path: &PathBuf, bytes: &[u8], mut streamed: Face<impl Source>, label: &str) {
    let reference = ttf_parser::Face::parse(bytes, 0).unwrap();
    let cmap = reference.tables().cmap.unwrap();
    let selected = cmap
        .subtables
        .into_iter()
        .find(|s| {
            s.is_unicode()
                && match s.format {
                    ttf_parser::cmap::Format::SegmentMappingToDeltaValues(_) => {
                        streamed.cmap_format() == 4
                    }
                    ttf_parser::cmap::Format::SegmentedCoverage(_) => streamed.cmap_format() == 12,
                    _ => false,
                }
        })
        .unwrap();
    for cp in 0..=0x10ffff {
        let expect = selected.glyph_index(cp).filter(|g| g.0 != 0).map(|g| g.0);
        assert_eq!(
            streamed.glyph_index(cp).unwrap(),
            expect,
            "{label} {path:?} U+{cp:04X}"
        );
        if let Some(ch) = char::from_u32(cp) {
            assert_eq!(
                expect,
                reference.glyph_index(ch).filter(|g| g.0 != 0).map(|g| g.0),
                "Face cmap {label} U+{cp:04X}"
            );
        }
    }
    let raw = reference.raw_face();
    let loca = ttf_parser::loca::Table::parse(
        std::num::NonZeroU16::new(reference.number_of_glyphs()).unwrap(),
        reference.tables().head.index_to_location_format,
        raw.table(Tag::from_bytes(b"loca")).unwrap(),
    )
    .unwrap();
    let gbase = streamed.table_range(*b"glyf").unwrap().start;
    for gid in 0..reference.number_of_glyphs() {
        let range = streamed.glyph_range(gid).unwrap();
        match loca.glyph_range(GlyphId(gid)) {
            Some(r) => assert_eq!(range, gbase + r.start as u64..gbase + r.end as u64),
            None => assert!(range.is_empty()),
        }
        assert_eq!(
            streamed.advance(gid).unwrap(),
            reference.glyph_hor_advance(GlyphId(gid)).unwrap()
        );
    }
    let metrics = streamed.metrics();
    assert_eq!(
        (
            metrics.units_per_em,
            metrics.ascender,
            metrics.descender,
            metrics.line_gap
        ),
        (
            reference.units_per_em(),
            reference.ascender(),
            reference.descender(),
            reference.line_gap()
        )
    );
    println!(
        "PASS {label} {} format{}: 1114112 codepoints, {} loca/advances",
        path.file_name().unwrap().to_string_lossy(),
        streamed.cmap_format(),
        reference.number_of_glyphs()
    );
}
#[test]
fn real_fonts_all_codepoints_loca_advance() {
    for path in fonts() {
        let bytes = fs::read(&path)
            .expect("fetch_fonts.py must obtain real fonts before tests; never skip");
        let (source, stats) = disk(&path);
        let face = Face::open(source).unwrap();
        compare(&path, &bytes, face, "selected");
        assert!(stats.max.get() <= READ_CACHE_SLOT_BYTES);
        println!(
            "pread count={} max request={}",
            stats.reads.get(),
            stats.max.get()
        );
        // Exercise IPA format4 too: suppress format12 encoding records in both
        // inputs. All table bytes/offsets remain identical to the real original.
        let mut forced = bytes.clone();
        let c = table(&forced, b"cmap");
        for i in 0..r16(&forced, c + 2) as usize {
            let at = c + 4 + i * 8;
            let off = r32(&forced, at + 4) as usize;
            if r16(&forced, c + off) != 4 {
                w16(&mut forced, at, 99);
            }
        }
        let f = Face::open(memory(forced.clone())).unwrap();
        assert_eq!(f.cmap_format(), 4);
        compare(&path, &forced, f, "format4");
    }
}
#[test]
fn short_reads_publish_nothing() {
    let path = fonts().remove(1);
    let (source, stats) = disk(&path);
    let f = Face::open(source).unwrap();
    drop(f);
    let reads = stats.reads.get();
    for at in 1..=reads {
        let (source, stats) = disk(&path);
        stats.fail.set(Some(at));
        assert!(
            matches!(Face::open(source), Err(Error::ShortRead)),
            "short read {at}/{reads} published face"
        );
    }
    let (source, stats) = disk(&path);
    let mut f = Face::open(source).unwrap();
    stats.short.set(true);
    for op in 0..3 {
        f.trim();
        let result = match op {
            0 => f.glyph_index(0x65e5).map(|_| ()),
            1 => f.glyph_range(10).map(|_| ()),
            _ => f.advance(10).map(|_| ()),
        };
        assert_eq!(
            result,
            Err(Error::ShortRead),
            "short read published glyph operation {op}"
        );
    }
    stats.short.set(false);
    // The failed fill was invalidated, so retry needs no explicit trim.
    assert!(f.advance(10).is_ok());
    f.trim();
    assert!(f.glyph_index(0x65e5).unwrap().is_some());
    println!("PASS short reads: all {reads} open fills + lookup/loca/advance; retry after trim");
}

#[test]
fn read_cache_lru() {
    let (source, stats) = disk(&fonts().remove(1)); // IPAexG
    let mut face = Face::open(source).unwrap();
    // Fill all N slots, promote the oldest, then force an eviction.
    face.trim();
    stats.reads.set(0);
    for block in 0..READ_CACHE_SLOTS {
        face.advance((block * 512) as u16).unwrap();
    }
    face.advance(0).unwrap();
    face.advance((READ_CACHE_SLOTS * 512) as u16).unwrap();
    face.advance(0).unwrap();
    assert_eq!(stats.reads.get(), READ_CACHE_SLOTS + 1);
}
