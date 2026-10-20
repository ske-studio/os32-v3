//! TASK_FONTPERF F3 (fontperf design §2): the guest reader of the pre-drawn
//! 16px glyph bundle (.g16, tools/gen_g16.py; the byte map is its docstring).
//!
//! `open` reads the 128 B header and the 8,704 B rank index (10 read_at of at
//! most G16_READ_BYTES), and accepts the bundle only when the magic, version,
//! cell/ascent/record sizes, section bounds, the TTF identity (size,
//! head.checkSumAdjustment, head.modified of the open ja face, U1 (b)) and the
//! index CRC-32 (F-7) all match; otherwise the source is dropped and
//! `stats.rejected` holds the reason. `lookup` maps a BMP code point to its
//! record (cp > 0xFFFF: None), `fetch` reads one 32 B cell (one read_at, one
//! sector), `preload` reads the preload section in order and hands each record
//! whose cp the index holds to the caller. G16_ERR_CLOSE consecutive record
//! read failures close the bundle. The header is parsed from bytes with
//! from_le_bytes (off_all at 86 and modified at 28 are not aligned).
//! An all-zero Bundle equals `Bundle::new()` (closed, no source).
use crate::limits::*;
use crate::reader::{read_exact_at, Result, Source};

const MAGIC: [u8; 4] = *b"OSG1";
// Header field offsets (tools/gen_g16.py docstring).
const H_VERSION: usize = 4;
const H_HDR_LEN: usize = 6;
const H_CELL_W: usize = 8;
const H_CELL_H: usize = 9;
const H_ASCENT: usize = 10;
const H_FLAGS: usize = 11;
const H_N_ALL: usize = 12;
const H_N_PRE: usize = 16;
const H_TTF_SIZE: usize = 20;
const H_TTF_ADJUST: usize = 24;
const H_TTF_MODIFIED: usize = 28;
const H_OFF_RANK: usize = 68;
const H_RANK_LEN: usize = 72;
const H_RANK_CRC: usize = 76;
const H_OFF_PRE: usize = 80;
const H_PRE_REC: usize = 84;
const H_OFF_ALL: usize = 86;
const H_ALL_REC: usize = 90;
const FLAG_BMP_ONLY: u8 = 1;
const CRC32_POLY: u32 = 0xEDB8_8320;
const BITS_PER_BLOCK_BYTES: usize = G16_RANK_BITMAP_BYTES / G16_RANK_BLOCKS;

/// Why the last `open` refused the bundle (`stats.rejected`; 0 = accepted).
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Reject {
    None = 0,
    /// A read failed or came short (the file is shorter than its header says).
    Io = 1,
    Magic = 2,
    Version = 3,
    /// Cell size, ascent, record sizes, flags or section bounds.
    Layout = 4,
    /// Not made from the open ja face (size, head.checkSumAdjustment, head.modified).
    Head = 5,
    IndexCrc = 6,
    /// The index counts disagree with n_all.
    Index = 7,
    /// The engine had no open face set (or was busy).
    NotOpen = 8,
}

/// Bundle counters, appended to `AppFontStats` / `gshell_font_stats` (G-3).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct BundleStats {
    /// 1 while a bundle is open and accepted.
    pub ok: u32,
    /// `Reject` of the last open (0 = accepted or never opened).
    pub rejected: u32,
    /// Misses answered from the bundle.
    pub hit: u32,
    /// Full 16px misses whose cp the bundle does not hold (TTF path).
    pub miss: u32,
    /// Record read failures and preload records whose cp the index lacks.
    pub err: u32,
}

/// Result of one record fetch.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Fetch {
    Hit,
    Miss,
    Err,
}

pub struct Bundle<S> {
    src: Option<S>,
    rank: [u8; G16_RANK_BITMAP_BYTES],
    cum: [u16; G16_RANK_BLOCKS],
    n_all: u32,
    n_pre: u32,
    off_pre: u32,
    off_all: u32,
    ok: bool,
    errs: u8,
    stats: BundleStats,
}

impl<S> Default for Bundle<S> {
    fn default() -> Self {
        Self::new()
    }
}

fn le16(b: &[u8], at: usize) -> u16 {
    b.get(at..at + 2)
        .and_then(|s| s.try_into().ok())
        .map(u16::from_le_bytes)
        .unwrap_or(0)
}
fn le32(b: &[u8], at: usize) -> u32 {
    b.get(at..at + 4)
        .and_then(|s| s.try_into().ok())
        .map(u32::from_le_bytes)
        .unwrap_or(0)
}
fn le64(b: &[u8], at: usize) -> u64 {
    u64::from(le32(b, at)) | u64::from(le32(b, at + 4)) << 32
}
fn byte_at(b: &[u8], at: usize) -> u8 {
    b.get(at).copied().unwrap_or(0)
}

/// zlib CRC-32 (reflected, 0xEDB88320), bitwise: the index is read once per open.
fn crc32_update(mut crc: u32, data: &[u8]) -> u32 {
    for &b in data {
        crc ^= u32::from(b);
        for _ in 0..8 {
            let mask = (crc & 1).wrapping_neg();
            crc = (crc >> 1) ^ (CRC32_POLY & mask);
        }
    }
    crc
}

impl<S> Bundle<S> {
    pub const fn new() -> Self {
        Self {
            src: None,
            rank: [0; G16_RANK_BITMAP_BYTES],
            cum: [0; G16_RANK_BLOCKS],
            n_all: 0,
            n_pre: 0,
            off_pre: 0,
            off_all: 0,
            ok: false,
            errs: 0,
            stats: BundleStats {
                ok: 0,
                rejected: 0,
                hit: 0,
                miss: 0,
                err: 0,
            },
        }
    }
    pub fn is_ok(&self) -> bool {
        self.ok
    }
    /// True while a source is held (open, accepted).
    pub fn has_source(&self) -> bool {
        self.src.is_some()
    }
    pub fn stats(&self) -> BundleStats {
        self.stats
    }
    /// (n_all, n_pre) of the open bundle (0, 0 when closed).
    pub fn counts(&self) -> (u32, u32) {
        if self.ok {
            (self.n_all, self.n_pre)
        } else {
            (0, 0)
        }
    }
    /// Drop the source (the owner's lease closes through its Drop); counters stay.
    pub fn close(&mut self) {
        self.src = None;
        self.ok = false;
        self.errs = 0;
        self.n_all = 0;
        self.n_pre = 0;
        self.stats.ok = 0;
    }
    /// Refuse without a source to read (the caller drops it).
    pub fn reject(&mut self, why: Reject) {
        self.close();
        self.stats.rejected = why as u32;
    }

    /// Record number of `cp` in the all section (None: not in the bundle, or above the BMP).
    pub fn lookup(&self, cp: u32) -> Option<u32> {
        if !self.ok || cp as usize >= G16_BMP_CPS {
            return None;
        }
        let cp = cp as usize;
        let at = cp >> 3;
        let bit = cp & 7;
        let byte = *self.rank.get(at)?;
        if (byte >> bit) & 1 == 0 {
            return None;
        }
        let block = cp >> 8;
        let mut n = u32::from(*self.cum.get(block)?);
        for b in self.rank.get(block * BITS_PER_BLOCK_BYTES..at)? {
            n += b.count_ones();
        }
        n += (byte & ((1u8 << bit) - 1)).count_ones();
        if n < self.n_all {
            Some(n)
        } else {
            None
        }
    }
}

impl<S: Source> Bundle<S> {
    /// Open `src` against the open ja face's identity `(size, adjust, modified)`
    /// (None: no face). On any mismatch the source is dropped.
    pub fn open(&mut self, src: S, ttf: Option<(u64, u32, u64)>) -> core::result::Result<(), Reject> {
        self.close();
        self.src = Some(src);
        match self.load(ttf) {
            Ok(()) => {
                self.ok = true;
                self.stats.ok = 1;
                self.stats.rejected = Reject::None as u32;
                Ok(())
            }
            Err(why) => {
                self.reject(why);
                Err(why)
            }
        }
    }

    /// The read buffer lives on the caller's stack (G16_READ_BYTES): the
    /// resident image has no room for it (gshell, fontperf design §5).
    fn read(&mut self, offset: u64, dst: &mut [u8]) -> core::result::Result<(), Reject> {
        let src = self.src.as_mut().ok_or(Reject::Io)?;
        read_exact_at(src, offset, dst).map_err(|_| Reject::Io)
    }

    #[inline(never)]
    fn load(&mut self, ttf: Option<(u64, u32, u64)>) -> core::result::Result<(), Reject> {
        let size = self.src.as_ref().ok_or(Reject::Io)?.file_size();
        let mut buf = [0u8; G16_READ_BYTES];
        let h = buf.get_mut(..G16_HDR_BYTES).ok_or(Reject::Io)?;
        self.read(0, h)?;
        let h = buf.get(..G16_HDR_BYTES).ok_or(Reject::Io)?;
        if h.get(..MAGIC.len()) != Some(MAGIC.as_slice()) {
            return Err(Reject::Magic);
        }
        if le16(h, H_VERSION) != G16_VERSION || usize::from(le16(h, H_HDR_LEN)) != G16_HDR_BYTES {
            return Err(Reject::Version);
        }
        let n_all = le32(h, H_N_ALL);
        let n_pre = le32(h, H_N_PRE);
        let off_rank = le32(h, H_OFF_RANK);
        let off_pre = le32(h, H_OFF_PRE);
        let off_all = le32(h, H_OFF_ALL);
        let rank_crc = le32(h, H_RANK_CRC);
        let ttf_size = le32(h, H_TTF_SIZE);
        let ttf_adjust = le32(h, H_TTF_ADJUST);
        let ttf_modified = le64(h, H_TTF_MODIFIED);
        let rank_end = u64::from(off_rank) + G16_RANK_BYTES as u64;
        let pre_end = u64::from(off_pre) + u64::from(n_pre) * G16_PRE_REC as u64;
        let all_end = u64::from(off_all) + u64::from(n_all) * G16_ALL_REC as u64;
        let layout = byte_at(h, H_CELL_W) == G16_CELL_PX
            && byte_at(h, H_CELL_H) == G16_CELL_PX
            && i16::from(byte_at(h, H_ASCENT) as i8) == ASCENT_JA_PX
            && byte_at(h, H_FLAGS) & FLAG_BMP_ONLY != 0
            && usize::from(le16(h, H_PRE_REC)) == G16_PRE_REC
            && usize::from(le16(h, H_ALL_REC)) == G16_ALL_REC
            && le32(h, H_RANK_LEN) as usize == G16_RANK_BYTES
            && off_rank as usize >= G16_HDR_BYTES
            && u64::from(off_pre) >= rank_end
            && u64::from(off_all) >= pre_end
            && off_all % G16_SECTOR == 0
            && n_all as usize <= G16_BMP_CPS
            && all_end <= size;
        if !layout {
            return Err(Reject::Layout);
        }
        match ttf {
            Some((s, adjust, modified))
                if s == u64::from(ttf_size) && adjust == ttf_adjust && modified == ttf_modified => {}
            _ => return Err(Reject::Head),
        }
        // Rank index: G16_READ_BYTES at a time, bitmap then cum (u16 LE).
        let mut crc = !0u32;
        let mut done = 0usize;
        while done < G16_RANK_BYTES {
            let len = (G16_RANK_BYTES - done).min(G16_READ_BYTES);
            let chunk = buf.get_mut(..len).ok_or(Reject::Io)?;
            self.read(u64::from(off_rank) + done as u64, chunk)?;
            let chunk = buf.get(..len).ok_or(Reject::Io)?;
            crc = crc32_update(crc, chunk);
            for (k, &b) in chunk.iter().enumerate() {
                let at = done + k;
                if let Some(r) = self.rank.get_mut(at) {
                    *r = b;
                } else {
                    let i = at - G16_RANK_BITMAP_BYTES;
                    if let Some(c) = self.cum.get_mut(i / 2) {
                        *c = if i % 2 == 0 { (*c & 0xff00) | u16::from(b) } else { (*c & 0x00ff) | u16::from(b) << 8 };
                    }
                }
            }
            done += len;
        }
        if !crc != rank_crc {
            return Err(Reject::IndexCrc);
        }
        let last = G16_RANK_BLOCKS - 1;
        let tail: u32 = self
            .rank
            .get(last * BITS_PER_BLOCK_BYTES..)
            .map_or(0, |s| s.iter().map(|b| b.count_ones()).sum());
        if self.cum.get(last).map(|c| u32::from(*c) + tail) != Some(n_all) {
            return Err(Reject::Index);
        }
        self.n_all = n_all;
        self.n_pre = n_pre;
        self.off_pre = off_pre;
        self.off_all = off_all;
        self.errs = 0;
        Ok(())
    }

    fn note_err(&mut self) {
        self.stats.err = self.stats.err.wrapping_add(1);
        self.errs = self.errs.saturating_add(1);
        if self.errs >= G16_ERR_CLOSE {
            self.close();
        }
    }

    /// One Full 16px miss: the cell of `cp` into `out` (one read_at).
    pub fn fetch(&mut self, cp: u32, out: &mut [u8; G16_ALL_REC]) -> Fetch {
        let rec = match self.lookup(cp) {
            Some(r) => r,
            None => {
                self.stats.miss = self.stats.miss.wrapping_add(1);
                return Fetch::Miss;
            }
        };
        let at = u64::from(self.off_all) + u64::from(rec) * G16_ALL_REC as u64;
        let read: Result<()> = match self.src.as_mut() {
            Some(src) => read_exact_at(src, at, out),
            None => Err(crate::reader::Error::Closed),
        };
        if read.is_err() {
            self.note_err();
            return Fetch::Err;
        }
        self.errs = 0;
        self.stats.hit = self.stats.hit.wrapping_add(1);
        Fetch::Hit
    }

    /// Read the preload section in list order (whole records per read) and hand
    /// up to `max` records to `commit` (true = committed). A record whose cp the
    /// index lacks is not handed over (err). A read failure ends the preload.
    /// Returns the number committed.
    #[inline(never)]
    pub fn preload<F: FnMut(u32, &[u8; G16_ALL_REC]) -> bool>(&mut self, max: usize, mut commit: F) -> u32 {
        if !self.ok {
            return 0;
        }
        let mut buf = [0u8; G16_READ_BYTES];
        let total = (self.n_pre as usize).min(max);
        let per_read = G16_READ_BYTES / G16_PRE_REC;
        let mut done = 0usize;
        let mut committed = 0u32;
        while done < total {
            let n = (total - done).min(per_read);
            let at = u64::from(self.off_pre) + (done * G16_PRE_REC) as u64;
            let ok = match buf.get_mut(..n * G16_PRE_REC) {
                Some(dst) => self.read(at, dst).is_ok(),
                None => false,
            };
            if !ok {
                self.note_err();
                break;
            }
            for k in 0..n {
                let mut cell = [0u8; G16_ALL_REC];
                let rec = match buf.get(k * G16_PRE_REC..(k + 1) * G16_PRE_REC) {
                    Some(r) => r,
                    None => break,
                };
                let cp = le32(rec, 0);
                if let Some(bits) = rec.get(4..) {
                    cell.copy_from_slice(bits);
                }
                if self.lookup(cp).is_none() {
                    self.stats.err = self.stats.err.wrapping_add(1);
                    continue;
                }
                if commit(cp, &cell) {
                    committed += 1;
                }
            }
            done += n;
        }
        committed
    }
}
