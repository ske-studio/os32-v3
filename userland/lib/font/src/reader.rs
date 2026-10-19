//! Bounded SFNT table reader. No file/table-sized allocation is performed.
//! A Source belongs to one open font and must keep its contents stable until close.
//! The VFS adapter (b1) is responsible for owner/FD-generation checks on each read.
use crate::limits::*;
use core::ops::Range;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Bounds,
    ShortRead,
    Io,
    Invalid,
    Unsupported,
    Limit,
    /// The face is not open (never opened, closed, or its last open failed).
    Closed,
}
pub type Result<T> = core::result::Result<T, Error>;

pub trait Source {
    fn file_size(&self) -> u64;
    /// Return the actual byte count, including short reads; never retry here.
    fn read_at(&mut self, offset: u64, dst: &mut [u8]) -> Result<usize>;
}

/// Check subtraction before calling the source, including zero-length requests.
pub fn read_exact_at<S: Source>(source: &mut S, offset: u64, dst: &mut [u8]) -> Result<()> {
    let size = source.file_size();
    if offset > size || dst.len() as u64 > size - offset {
        return Err(Error::Bounds);
    }
    if dst.is_empty() {
        return Ok(());
    }
    if source.read_at(offset, dst)? != dst.len() {
        return Err(Error::ShortRead);
    }
    Ok(())
}

struct Slot {
    start: u64,
    valid: usize,
    lru: usize,
    data: [u8; READ_CACHE_SLOT_BYTES],
}
impl Slot {
    const EMPTY: Self = Self {
        start: 0,
        valid: 0,
        // An empty slot's rank is never read (fills reset it before ranking). Zero
        // keeps a closed Face all-zero, so a static Face lands in .bss, not .data.
        lru: 0,
        data: [0; READ_CACHE_SLOT_BYTES],
    };
}

struct Reader<S> {
    source: Option<S>,
    slots: [Slot; READ_CACHE_SLOTS],
}
impl<S> Reader<S> {
    const CLOSED: Self = Self {
        source: None,
        slots: [const { Slot::EMPTY }; READ_CACHE_SLOTS],
    };
    fn invalidate(&mut self) {
        for slot in &mut self.slots {
            slot.valid = 0;
        }
    }
}
impl<S: Source> Reader<S> {
    fn size(&self) -> Result<u64> {
        Ok(self.source.as_ref().ok_or(Error::Closed)?.file_size())
    }

    fn read(&mut self, offset: u64, dst: &mut [u8]) -> Result<()> {
        let size = self.size()?;
        if offset > size || dst.len() as u64 > size - offset {
            return Err(Error::Bounds);
        }
        let mut pos = offset;
        let mut rest = dst;
        while !rest.is_empty() {
            let start = pos / READ_CACHE_SLOT_BYTES as u64 * READ_CACHE_SLOT_BYTES as u64;
            let within = (pos - start) as usize;
            let cached = self
                .slots
                .iter()
                .position(|s| s.valid != 0 && s.start == start);
            let index = match cached {
                Some(index) => index,
                None => {
                    let index = self
                        .slots
                        .iter()
                        .position(|s| s.valid == 0)
                        .or_else(|| {
                            self.slots
                                .iter()
                                .enumerate()
                                .max_by_key(|(_, s)| s.lru)
                                .map(|(i, _)| i)
                        })
                        .ok_or(Error::Invalid)?;
                    let slot = self.slots.get_mut(index).ok_or(Error::Invalid)?;
                    // Invalidate before I/O: partial data must never become a cache hit.
                    slot.valid = 0;
                    slot.lru = READ_CACHE_SLOTS;
                    let len = (size - start).min(READ_CACHE_SLOT_BYTES as u64) as usize;
                    let bytes = slot.data.get_mut(..len).ok_or(Error::Bounds)?;
                    read_exact_at(self.source.as_mut().ok_or(Error::Closed)?, start, bytes)?;
                    slot.start = start;
                    slot.valid = len;
                    index
                }
            };
            // Rank 0 is MRU. Promote hits as well as fills, with no clock overflow.
            let rank = self.slots.get(index).ok_or(Error::Invalid)?.lru;
            for slot in &mut self.slots {
                if slot.valid != 0 && slot.lru < rank {
                    slot.lru += 1;
                }
            }
            self.slots.get_mut(index).ok_or(Error::Invalid)?.lru = 0;
            let slot = self.slots.get(index).ok_or(Error::Invalid)?;
            let count = rest
                .len()
                .min(slot.valid.checked_sub(within).ok_or(Error::Bounds)?);
            if count == 0 {
                return Err(Error::Bounds);
            }
            let input = slot.data.get(within..within + count).ok_or(Error::Bounds)?;
            let output = rest.get_mut(..count).ok_or(Error::Bounds)?;
            output.copy_from_slice(input);
            rest = rest.get_mut(count..).ok_or(Error::Bounds)?;
            pos += count as u64;
        }
        Ok(())
    }
}

#[derive(Clone, Copy)]
struct Table {
    tag: u32,
    offset: u32,
    length: u32,
}
impl Table {
    const NONE: Self = Self {
        tag: 0,
        offset: 0,
        length: 0,
    };
    fn at(self, offset: u32, len: u32) -> Result<u64> {
        if offset > self.length || len > self.length - offset {
            return Err(Error::Bounds);
        }
        Ok(u64::from(self.offset) + u64::from(offset))
    }
    fn sub(self, offset: u32, length: u32) -> Result<Self> {
        let absolute = self.at(offset, length)?;
        Ok(Self {
            tag: self.tag,
            offset: u32::try_from(absolute).map_err(|_| Error::Bounds)?,
            length,
        })
    }
}

fn be16(bytes: &[u8], offset: usize) -> Result<u16> {
    let data = bytes.get(offset..offset + 2).ok_or(Error::Bounds)?;
    Ok(u16::from_be_bytes(
        data.try_into().map_err(|_| Error::Bounds)?,
    ))
}
fn be32(bytes: &[u8], offset: usize) -> Result<u32> {
    let high = be16(bytes, offset)?;
    let low = be16(bytes, offset.checked_add(2).ok_or(Error::Bounds)?)?;
    // D36 (386 floor): combine the halves through an optimisation barrier so
    // LLVM cannot fold them into a 32-bit load + BSWAP (486+). The final
    // linked ELF is checked by tools/check_isa386.py.
    Ok(u32::from(core::hint::black_box(high)) << 16 | u32::from(low))
}
fn u16_at<S: Source>(r: &mut Reader<S>, t: Table, offset: u32) -> Result<u16> {
    let mut bytes = [0; 2];
    r.read(t.at(offset, 2)?, &mut bytes)?;
    be16(&bytes, 0)
}
fn u32_at<S: Source>(r: &mut Reader<S>, t: Table, offset: u32) -> Result<u32> {
    let mut bytes = [0; 4];
    r.read(t.at(offset, 4)?, &mut bytes)?;
    be32(&bytes, 0)
}

#[derive(Clone, Copy)]
struct Cmap {
    table: Table,
    format: u16,
    count: u32,
}
impl Cmap {
    const NONE: Self = Self {
        table: Table::NONE,
        format: 0,
        count: 0,
    };
}

/// The selected cmap subtable's search arrays, copied once at open: format 4
/// endCode..idRangeOffset (subtable bytes 14..16 + 8 * segments), format 12 the
/// groups (16..16 + 12 * groups). glyphIdArray is never copied (read_at as before).
/// `len == 0` means the face uses the read_at path for the whole cmap.
struct Resident {
    base: u32,
    len: usize,
    bytes: [u8; MAX_CMAP_RESIDENT_BYTES],
}
impl Resident {
    const EMPTY: Self = Self {
        base: 0,
        len: 0,
        bytes: [0; MAX_CMAP_RESIDENT_BYTES],
    };
    /// The resident copy of subtable bytes [offset, offset + len), if fully inside it.
    fn get(&self, offset: u32, len: usize) -> Option<&[u8]> {
        let rel = usize::try_from(offset.checked_sub(self.base)?).ok()?;
        let end = rel.checked_add(len)?;
        if end > self.len {
            return None;
        }
        self.bytes.get(rel..end)
    }
}

/// Selected-subtable read: the resident copy when it covers the bytes, else read_at.
fn cmap_read<S: Source>(
    r: &mut Reader<S>,
    res: &Resident,
    t: Table,
    offset: u32,
    dst: &mut [u8],
) -> Result<()> {
    if let Some(bytes) = res.get(offset, dst.len()) {
        dst.copy_from_slice(bytes);
        return Ok(());
    }
    let len = u32::try_from(dst.len()).map_err(|_| Error::Bounds)?;
    r.read(t.at(offset, len)?, dst)
}
fn cmap_u16<S: Source>(r: &mut Reader<S>, res: &Resident, t: Table, offset: u32) -> Result<u16> {
    let mut bytes = [0; 2];
    cmap_read(r, res, t, offset, &mut bytes)?;
    be16(&bytes, 0)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Metrics {
    pub units_per_em: u16,
    pub ascender: i16,
    pub descender: i16,
    pub line_gap: i16,
}
impl Metrics {
    const NONE: Self = Self {
        units_per_em: 0,
        ascender: 0,
        descender: 0,
        line_gap: 0,
    };
}

/// Owns its source and READ_CACHE_SLOTS fixed LRU read-cache slots of READ_CACHE_SLOT_BYTES
/// each, plus MAX_CMAP_RESIDENT_BYTES for the resident cmap arrays.
/// T7b-a7: a Face is never returned by value (tens of KiB; gshell's stack is 40KiB).
/// The caller owns the storage (a static, an arena slot, a Box on the host):
/// `Face::new()` is a closed face usable in a static initializer, and `open`
/// fills it in place. Closing (or reopening, or dropping) drops the source, which
/// closes an owning adapter through its usual Drop; no other FD or memory registry.
pub struct Face<S> {
    reader: Reader<S>,
    tables: [Table; MAX_TABLES],
    table_count: usize,
    cmap: Cmap,
    resident: Resident,
    loca: Table,
    glyf: Table,
    hmtx: Table,
    long_loca: bool,
    glyphs: u16,
    horizontal_metrics: u16,
    metrics: Metrics,
    open: bool,
}

impl<S> Face<S> {
    /// A closed face. Lookups fail with `Error::Closed` until `open` succeeds.
    pub const fn new() -> Self {
        Self {
            reader: Reader::CLOSED,
            tables: [Table::NONE; MAX_TABLES],
            table_count: 0,
            cmap: Cmap::NONE,
            resident: Resident::EMPTY,
            loca: Table::NONE,
            glyf: Table::NONE,
            hmtx: Table::NONE,
            long_loca: false,
            glyphs: 0,
            horizontal_metrics: 0,
            metrics: Metrics::NONE,
            open: false,
        }
    }
    /// Drop the source and every cached/resident byte; the storage stays the caller's.
    pub fn close(&mut self) {
        self.open = false;
        self.reader.source = None;
        self.reader.invalidate();
        self.resident.len = 0;
        self.table_count = 0;
        self.cmap = Cmap::NONE;
        self.loca = Table::NONE;
        self.glyf = Table::NONE;
        self.hmtx = Table::NONE;
        self.long_loca = false;
        self.glyphs = 0;
        self.horizontal_metrics = 0;
        self.metrics = Metrics::NONE;
    }
    pub fn is_open(&self) -> bool {
        self.open
    }
    /// True when the cmap search arrays are resident (the face fitted MAX_CMAP_RESIDENT_BYTES).
    pub fn cmap_resident(&self) -> bool {
        self.open && self.resident.len != 0
    }
    pub fn metrics(&self) -> Metrics {
        self.metrics
    }
    pub fn number_of_glyphs(&self) -> u16 {
        self.glyphs
    }
    pub fn cmap_format(&self) -> u16 {
        self.cmap.format
    }
    pub fn table_count(&self) -> usize {
        self.table_count
    }
    /// A bounded directory view, useful to later glyph readers without raw indexing.
    pub fn table_range(&self, tag: [u8; 4]) -> Option<Range<u64>> {
        self.tables
            .iter()
            .take(self.table_count)
            .find(|t| t.tag == u32::from_be_bytes(tag))
            .map(|t| u64::from(t.offset)..u64::from(t.offset) + u64::from(t.length))
    }
    /// Discard cached bytes at the existing trim boundary; the source remains open.
    /// The resident cmap belongs to the open face and is kept.
    pub fn trim(&mut self) {
        self.reader.invalidate();
    }
    fn ensure_open(&self) -> Result<()> {
        if self.open {
            Ok(())
        } else {
            Err(Error::Closed)
        }
    }
}

impl<S> Default for Face<S> {
    fn default() -> Self {
        Self::new()
    }
}

impl<S: Source> Face<S> {
    /// Open `source` in place (closing any face held here first). The face is
    /// published only after directory, scalar and selected cmap validation; on
    /// failure it is left closed and `source` has been dropped.
    pub fn open(&mut self, source: S) -> Result<()> {
        self.open_with(source, true)
    }
    /// As `open`, but always on the read_at cmap path (the path a face whose cmap
    /// arrays exceed MAX_CMAP_RESIDENT_BYTES takes). Kept for differential tests.
    pub fn open_streaming(&mut self, source: S) -> Result<()> {
        self.open_with(source, false)
    }

    fn open_with(&mut self, source: S, resident: bool) -> Result<()> {
        self.close();
        self.reader.source = Some(source);
        match self.load(resident) {
            Ok(()) => {
                self.open = true;
                Ok(())
            }
            Err(error) => {
                self.close();
                Err(error)
            }
        }
    }

    fn load(&mut self, resident: bool) -> Result<()> {
        let r = &mut self.reader;
        let mut header = [0; 12];
        r.read(0, &mut header)?;
        if be32(&header, 0)? != 0x0001_0000 && be32(&header, 0)? != u32::from_be_bytes(*b"true") {
            return Err(Error::Unsupported);
        }
        let count = be16(&header, 4)? as usize;
        if count == 0 {
            return Err(Error::Invalid);
        }
        if count > MAX_TABLES {
            return Err(Error::Limit);
        }
        let directory_end = 12 + count as u32 * 16;
        let file_size = r.size()?;
        let mut previous = 0;
        for (i, out) in self.tables.iter_mut().take(count).enumerate() {
            let mut record = [0; 16];
            r.read(12 + i as u64 * 16, &mut record)?;
            let t = Table {
                tag: be32(&record, 0)?,
                offset: be32(&record, 8)?,
                length: be32(&record, 12)?,
            };
            // SFNT directory is ordered by tag, not by physical table offset.
            if t.tag <= previous {
                return Err(Error::Invalid);
            }
            previous = t.tag;
            let end = t.offset.checked_add(t.length).ok_or(Error::Bounds)?;
            if t.offset < directory_end || u64::from(end) > file_size {
                return Err(Error::Bounds);
            }
            *out = t;
        }
        let tables = &self.tables;
        let find = |tag: [u8; 4]| -> Result<Table> {
            tables
                .iter()
                .take(count)
                .find(|t| t.tag == u32::from_be_bytes(tag))
                .copied()
                .ok_or(Error::Unsupported)
        };
        if tables
            .iter()
            .take(count)
            .any(|t| t.tag == u32::from_be_bytes(*b"fvar"))
        {
            return Err(Error::Unsupported);
        }
        let head = find(*b"head")?;
        let hhea = find(*b"hhea")?;
        let maxp = find(*b"maxp")?;
        head.at(0, 54)?;
        hhea.at(0, 36)?;
        maxp.at(0, 32)?;
        if u32_at(r, head, 0)? != 0x0001_0000
            || u32_at(r, head, 12)? != 0x5f0f_3cf5
            || u32_at(r, hhea, 0)? != 0x0001_0000
            || u32_at(r, maxp, 0)? != 0x0001_0000
        {
            return Err(Error::Invalid);
        }
        let units = u16_at(r, head, 18)?;
        let location_format = u16_at(r, head, 50)?;
        if !(16..=16384).contains(&units) || location_format > 1 || u16_at(r, head, 52)? != 0 {
            return Err(Error::Invalid);
        }
        let glyphs = u16_at(r, maxp, 4)?;
        let horizontal_metrics = u16_at(r, hhea, 34)?;
        if glyphs == 0 || horizontal_metrics == 0 || horizontal_metrics > glyphs {
            return Err(Error::Invalid);
        }
        let loca = find(*b"loca")?;
        let glyf = find(*b"glyf")?;
        let hmtx = find(*b"hmtx")?;
        loca.at(
            0,
            (u32::from(glyphs) + 1) * if location_format == 1 { 4 } else { 2 },
        )?;
        hmtx.at(
            0,
            u32::from(horizontal_metrics) * 4 + u32::from(glyphs - horizontal_metrics) * 2,
        )?;
        let cmap_table = find(*b"cmap")?;
        let cmap = Self::open_cmap(r, &mut self.resident, cmap_table, glyphs, resident)?;
        let metrics = Metrics {
            units_per_em: units,
            ascender: u16_at(r, hhea, 4)? as i16,
            descender: u16_at(r, hhea, 6)? as i16,
            line_gap: u16_at(r, hhea, 8)? as i16,
        };
        self.table_count = count;
        self.cmap = cmap;
        self.loca = loca;
        self.glyf = glyf;
        self.hmtx = hmtx;
        self.long_loca = location_format == 1;
        self.glyphs = glyphs;
        self.horizontal_metrics = horizontal_metrics;
        self.metrics = metrics;
        Ok(())
    }

    /// Copy the search arrays [base, base + len) of subtable `t` when they fit.
    /// Requests stay at most READ_CACHE_SLOT_BYTES and bypass the read cache.
    fn make_resident(
        r: &mut Reader<S>,
        res: &mut Resident,
        t: Table,
        base: u32,
        len: u32,
        allowed: bool,
    ) -> Result<()> {
        res.len = 0;
        if !allowed || len as usize > MAX_CMAP_RESIDENT_BYTES {
            return Ok(());
        }
        let mut at = t.at(base, len)?;
        let dst = res.bytes.get_mut(..len as usize).ok_or(Error::Limit)?;
        let source = r.source.as_mut().ok_or(Error::Closed)?;
        for chunk in dst.chunks_mut(READ_CACHE_SLOT_BYTES) {
            read_exact_at(source, at, chunk)?;
            at += chunk.len() as u64;
        }
        res.base = base;
        res.len = len as usize;
        Ok(())
    }

    fn open_cmap(
        r: &mut Reader<S>,
        res: &mut Resident,
        table: Table,
        glyphs: u16,
        resident: bool,
    ) -> Result<Cmap> {
        if u16_at(r, table, 0)? != 0 {
            return Err(Error::Invalid);
        }
        let count = u16_at(r, table, 2)?;
        if usize::from(count) > MAX_CMAP_SUBTABLES {
            return Err(Error::Limit);
        }
        table.at(0, 4 + u32::from(count) * 8)?;
        let mut selected = None;
        let mut best = 0;
        for i in 0..u32::from(count) {
            let at = 4 + i * 8;
            let platform = u16_at(r, table, at)?;
            let encoding = u16_at(r, table, at + 2)?;
            let offset = u32_at(r, table, at + 4)?;
            if offset < 4 + u32::from(count) * 8 {
                return Err(Error::Bounds);
            }
            let format = u16_at(r, table, offset)?;
            let rank = match (platform, encoding, format) {
                (3, 10, 12) => 4,
                (0, _, 12) => 3,
                (3, 1, 4) => 2,
                (0, _, 4) => 1,
                _ => 0,
            };
            if rank > best {
                // Validate the chosen subtable below, without allocating its body.
                selected = Some((offset, format));
                best = rank;
            }
        }
        let (offset, format) = selected.ok_or(Error::Unsupported)?;
        let remaining = table.length.checked_sub(offset).ok_or(Error::Bounds)?;
        let tail = table.sub(offset, remaining)?;
        let length = if format == 4 {
            u32::from(u16_at(r, tail, 2)?)
        } else {
            u32_at(r, tail, 4)?
        };
        let t = tail.sub(0, length)?;
        if format == 12 {
            if u16_at(r, t, 2)? != 0 {
                return Err(Error::Invalid);
            }
            let count = u32_at(r, t, 12)?;
            if count == 0 {
                return Err(Error::Invalid);
            }
            if count as usize > MAX_CMAP_GROUPS {
                return Err(Error::Limit);
            }
            t.at(0, 16 + count * 12)?;
            Self::make_resident(r, res, t, 16, count * 12, resident)?;
            let mut previous_end = None;
            for i in 0..count {
                let (start, end, gid) = Self::group(r, res, t, i)?;
                if start > end || end > 0x10ffff || previous_end.is_some_and(|p| p >= start) {
                    return Err(Error::Invalid);
                }
                let last_gid = gid.checked_add(end - start).ok_or(Error::Bounds)?;
                if last_gid >= u32::from(glyphs) {
                    return Err(Error::Invalid);
                }
                previous_end = Some(end);
            }
            Ok(Cmap {
                table: t,
                format,
                count,
            })
        } else {
            let twice = u16_at(r, t, 6)?;
            if twice == 0 || twice % 2 != 0 {
                return Err(Error::Invalid);
            }
            let count = u32::from(twice / 2);
            if count as usize > MAX_CMAP_SEGMENTS {
                return Err(Error::Limit);
            }
            t.at(0, 16 + count * 8)?;
            if u16_at(r, t, 14 + count * 2)? != 0 {
                return Err(Error::Invalid);
            }
            // endCode (14), reservedPad, startCode, idDelta, idRangeOffset: contiguous.
            Self::make_resident(r, res, t, 14, count * 8 + 2, resident)?;
            let mut previous_end = None;
            for i in 0..count {
                let (start, end, delta, range) = Self::segment(r, res, t, count, i)?;
                if start > end || previous_end.is_some_and(|p| p >= u32::from(start)) {
                    return Err(Error::Invalid);
                }
                if range != 0 {
                    let first = 16 + count * 6 + i * 2 + u32::from(range);
                    let last = first + u32::from(end - start) * 2;
                    if range % 2 != 0 || first < 16 + count * 8 {
                        return Err(Error::Bounds);
                    }
                    t.at(last, 2)?;
                }
                for cp in u32::from(start)..=u32::from(end) {
                    let gid = Self::map4(r, res, t, count, i, cp as u16, (start, delta, range))?;
                    if gid >= glyphs {
                        return Err(Error::Invalid);
                    }
                }
                previous_end = Some(u32::from(end));
            }
            if previous_end != Some(0xffff) {
                return Err(Error::Invalid);
            }
            Ok(Cmap {
                table: t,
                format,
                count,
            })
        }
    }

    fn group(r: &mut Reader<S>, res: &Resident, t: Table, index: u32) -> Result<(u32, u32, u32)> {
        let at = 16 + index * 12;
        let mut data = [0; 12];
        cmap_read(r, res, t, at, &mut data)?;
        Ok((be32(&data, 0)?, be32(&data, 4)?, be32(&data, 8)?))
    }
    fn segment(
        r: &mut Reader<S>,
        res: &Resident,
        t: Table,
        count: u32,
        i: u32,
    ) -> Result<(u16, u16, u16, u16)> {
        Ok((
            cmap_u16(r, res, t, 16 + count * 2 + i * 2)?,
            cmap_u16(r, res, t, 14 + i * 2)?,
            cmap_u16(r, res, t, 16 + count * 4 + i * 2)?,
            cmap_u16(r, res, t, 16 + count * 6 + i * 2)?,
        ))
    }
    fn map4(
        r: &mut Reader<S>,
        res: &Resident,
        t: Table,
        count: u32,
        i: u32,
        cp: u16,
        values: (u16, u16, u16),
    ) -> Result<u16> {
        let (start, delta, range) = values;
        if range == 0 {
            return Ok(cp.wrapping_add(delta));
        }
        // glyphIdArray lies past the resident arrays: this read goes through read_at.
        let at = 16 + count * 6 + i * 2 + u32::from(range) + u32::from(cp - start) * 2;
        let gid = cmap_u16(r, res, t, at)?;
        Ok(if gid == 0 { 0 } else { gid.wrapping_add(delta) })
    }

    pub fn glyph_index(&mut self, codepoint: u32) -> Result<Option<u16>> {
        self.ensure_open()?;
        let c = self.cmap;
        let r = &mut self.reader;
        let res = &self.resident;
        let mut low = 0;
        let mut high = c.count;
        let gid = if c.format == 12 {
            while low < high {
                let mid = low + (high - low) / 2;
                let (start, end, gid) = Self::group(r, res, c.table, mid)?;
                if codepoint < start {
                    high = mid;
                } else if codepoint > end {
                    low = mid + 1;
                } else {
                    let value = gid.checked_add(codepoint - start).ok_or(Error::Bounds)?;
                    return self.valid_gid(value);
                }
            }
            0
        } else {
            if codepoint > 0xffff {
                return Ok(None);
            }
            while low < high {
                let mid = low + (high - low) / 2;
                if u32::from(cmap_u16(r, res, c.table, 14 + mid * 2)?) < codepoint {
                    low = mid + 1;
                } else {
                    high = mid;
                }
            }
            if low == c.count {
                return Ok(None);
            }
            let (start, _, delta, range) = Self::segment(r, res, c.table, c.count, low)?;
            if codepoint < u32::from(start) {
                return Ok(None);
            }
            u32::from(Self::map4(
                r,
                res,
                c.table,
                c.count,
                low,
                codepoint as u16,
                (start, delta, range),
            )?)
        };
        self.valid_gid(gid)
    }
    fn valid_gid(&self, gid: u32) -> Result<Option<u16>> {
        if gid >= u32::from(self.glyphs) {
            return Err(Error::Invalid);
        }
        Ok(if gid == 0 { None } else { Some(gid as u16) })
    }

    /// Read only the adjacent loca entries. Empty glyphs are valid empty ranges.
    pub fn glyph_range(&mut self, glyph: u16) -> Result<Range<u64>> {
        self.ensure_open()?;
        if glyph >= self.glyphs {
            return Err(Error::Bounds);
        }
        let width = if self.long_loca { 4 } else { 2 };
        let mut data = [0; 8];
        let dst = data.get_mut(..width * 2).ok_or(Error::Bounds)?;
        self.reader.read(
            self.loca
                .at(u32::from(glyph) * width as u32, (width * 2) as u32)?,
            dst,
        )?;
        let (start, end) = if self.long_loca {
            (be32(&data, 0)?, be32(&data, 4)?)
        } else {
            (
                u32::from(be16(&data, 0)?) * 2,
                u32::from(be16(&data, 2)?) * 2,
            )
        };
        if start > end {
            return Err(Error::Invalid);
        }
        self.glyf.at(start, end - start)?;
        Ok(u64::from(self.glyf.offset) + u64::from(start)
            ..u64::from(self.glyf.offset) + u64::from(end))
    }

    pub fn advance(&mut self, glyph: u16) -> Result<u16> {
        self.ensure_open()?;
        if glyph >= self.glyphs {
            return Err(Error::Bounds);
        }
        let metric = glyph.min(self.horizontal_metrics - 1);
        u16_at(&mut self.reader, self.hmtx, u32::from(metric) * 4)
    }

    /// Copy one bounded glyph through the existing LRU. No whole glyf residency.
    pub(crate) fn read_glyph(&mut self, glyph: u16, dst: &mut [u8]) -> Result<usize> {
        let range = self.glyph_range(glyph)?;
        let len = usize::try_from(range.end - range.start).map_err(|_| Error::Bounds)?;
        if len > MAX_GLYPH_SPAN_BYTES || len > dst.len() {
            return Err(Error::Limit);
        }
        self.reader
            .read(range.start, dst.get_mut(..len).ok_or(Error::Bounds)?)?;
        Ok(len)
    }
}
