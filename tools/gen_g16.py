#!/usr/bin/env python3
"""TASK_FONTPERF F1: the pre-drawn 16px glyph bundle (.g16) of the GUI Japanese face.

  python3 tools/gen_g16.py --ttf assets/fonts/JF-Dot-Shinonome16.ttf \\
      --preload build/out/preload16.txt -o assets/fonts/JF-Dot-Shinonome16.g16

The glyphs are the TTF's own 16ppem EBDT strike (the original dots, with the
smallGlyphMetrics bearings), placed in the 16x16 cell. T7b-a5 proved the
product raster == EBDT for every glyph (tools/tests/test_font_raster.py), and
check_g16 there re-proves bundle == EBDT == raster for every record.

Cell: 16 rows x pitch 2, MSB = left, row 0 = cell top = baseline - ASCENT (14,
font crate limits.rs ASCENT_JA_PX); column 0 = pen position.

Records: every cp of the cmap on the Full path of the engine (cp >= 0x80 and
not LATIN_HALF_FIRST..=LATIN_HALF_LAST, font crate engine.rs is_latin_half),
gid != 0. A glyph outside the cell is not stored and fails the run (rc 5).

File layout (little endian, design §2):

  header 128 B at 0
     0 magic "OSG1"          4 version u16 = 1     6 hdr_len u16 = 128
     8 cell_w u8 = 16        9 cell_h u8 = 16     10 ascent i8 = 14
    11 flags u8 (bit0: BMP only)
    12 n_all u32            16 n_pre u32
    20 ttf_size u32         24 ttf head.checkSumAdjustment u32
    28 ttf head.modified u64 (raw LONGDATETIME bits)
    36 ttf_sha256 [32]
    68 off_rank u32         72 rank_len u32      76 rank_crc32 u32 (zlib CRC-32)
    80 off_pre u32          84 pre_rec u16 = 36
    86 off_all u32          90 all_rec u16 = 32  92..127 zero
  rank index at off_rank (128), rank_len = 8,704 B
     8,192 B presence bitmap of the BMP: bit (cp & 7) of byte (cp >> 3), LSB first
       512 B cum u16[256]: number of present cps below block * 256
     rank(cp) = cum[cp >> 8] + popcount(present cps in [cp & ~0xFF, cp)) — the
     record number in the all section. cp > 0xFFFF has no record.
  preload section at off_pre (8,832): n_pre x (cp u32 + cell 32 B), list order
  all section at off_all (multiple of 512): n_all x cell 32 B, cp ascending,
     so one record never straddles a sector.

Exit codes: 0 ok, 2 bad input (preload cp not in the bundle, cp > 0xFFFF),
3 no 16ppem 1bpp EBDT strike, 4 fontTools missing, 5 glyphs outside the cell.
The output is deterministic (same TTF + same list -> same bytes).
"""
import argparse
import hashlib
import os
import struct
import sys
import zlib

MAGIC = b'OSG1'
VERSION = 1
HDR_LEN = 128
CELL_W = 16
CELL_H = 16
ASCENT = 14             # font crate limits.rs ASCENT_JA_PX
PPEM = 16
FLAG_BMP_ONLY = 0x01
PRE_REC = 36
ALL_REC = 32
SECTOR = 512
BMP = 0x10000
BLOCKS = 256
RANK_LEN = BMP // 8 + BLOCKS * 2
LATIN_HALF_FIRST = 0xA0  # font crate limits.rs
LATIN_HALF_LAST = 0x24F
HEADER = struct.Struct('<4sHHBBbBIIIIQ32sIIIIHIH')


class G16Error(Exception):
    def __init__(self, rc, message):
        super().__init__(message)
        self.rc = rc


def full_path(cp):
    """The engine's Full cell (engine.rs): not ASCII, not the half-width Latin range."""
    return cp >= 0x80 and not (LATIN_HALF_FIRST <= cp <= LATIN_HALF_LAST)


def head_identity(data):
    """(checkSumAdjustment, modified as u64) read from the raw head table."""
    num = struct.unpack_from('>H', data, 4)[0]
    for i in range(num):
        tag, _, off, length = struct.unpack_from('>4sIII', data, 12 + 16 * i)
        if tag == b'head':
            if length < 36:
                raise G16Error(2, 'head table too short')
            adjust = struct.unpack_from('>I', data, off + 8)[0]
            modified = struct.unpack_from('>Q', data, off + 28)[0]
            return adjust, modified
    raise G16Error(2, 'no head table')


def strike_cells(font):
    """cell(gid) -> 32-byte cell from the 16ppem 1bpp EBDT strike, None if outside the cell."""
    if 'EBLC' not in font or 'EBDT' not in font:
        raise G16Error(3, 'no EBDT/EBLC: the bundle is made only from the embedded 16px dots')
    strikes = [i for i, s in enumerate(font['EBLC'].strikes)
               if s.bitmapSizeTable.ppemX == PPEM and s.bitmapSizeTable.ppemY == PPEM
               and s.bitmapSizeTable.bitDepth == 1]
    if not strikes:
        raise G16Error(3, 'no 16ppem 1bpp EBDT strike')
    strike = strikes[0]
    metrics = {}
    for sub in font['EBLC'].strikes[strike].indexSubTables:
        for gid in range(sub.firstGlyphIndex, sub.lastGlyphIndex + 1):
            metrics[gid] = getattr(sub, 'metrics', None)
    order = font.getGlyphOrder()
    data = font['EBDT'].strikeData[strike]

    def cell(gid):
        out = bytearray(32)
        if gid not in metrics or order[gid] not in data:
            return bytes(out)          # no dots in the strike: blank cell
        g = data[order[gid]]
        m = metrics[gid] or g.metrics
        if g.getFormat() not in (5, 7):
            raise G16Error(3, 'EBDT image format %d (bit-aligned 5/7 expected)' % g.getFormat())
        bits = g.imageData
        for r in range(m.height):
            for c in range(m.width):
                i = r * m.width + c
                if not bits[i // 8] >> (7 - i % 8) & 1:
                    continue
                x = m.horiBearingX + c
                row = ASCENT - m.horiBearingY + r
                if not (0 <= x < CELL_W and 0 <= row < CELL_H):
                    return None
                out[row * 2 + x // 8] |= 0x80 >> (x % 8)
        return bytes(out)
    return cell


def read_preload(path):
    cps = []
    for line in open(path, encoding='utf-8'):
        fields = line.split()
        if fields and not fields[0].startswith('#'):
            cps.append(int(fields[0], 16))   # `XXXX  c`: the hex field only
    return cps


def rank_index(cps):
    bitmap = bytearray(BMP // 8)
    for cp in cps:
        bitmap[cp >> 3] |= 1 << (cp & 7)
    cum, n = [], 0
    for block in range(BLOCKS):
        cum.append(n)   # <= 255 * 256: always fits u16
        n += sum(bin(b).count('1') for b in bitmap[block * 32:(block + 1) * 32])
    return bytes(bitmap) + struct.pack('<256H', *cum)


def build(ttf_path, preload_path):
    """(bundle bytes, report dict) or G16Error."""
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        raise G16Error(4, 'fontTools is not importable: pip install fonttools (requirements.txt)')
    raw = open(ttf_path, 'rb').read()
    font = TTFont(ttf_path)
    cmap = font.getBestCmap()
    order = font.getGlyphOrder()
    gid_of = {name: i for i, name in enumerate(order)}
    cell_of = strike_cells(font)
    cps = sorted(cp for cp, name in cmap.items() if full_path(cp) and gid_of[name] != 0)
    big = [cp for cp in cps if cp >= BMP]
    if big:
        raise G16Error(2, 'cp above the BMP in the cmap: %s' % ', '.join('U+%X' % cp for cp in big[:8]))
    records, outside = {}, []
    for cp in cps:
        c = cell_of(gid_of[cmap[cp]])
        if c is None:
            outside.append(cp)
        else:
            records[cp] = c
    if outside:
        raise G16Error(5, '%d glyph(s) outside the 16x16 cell: %s' % (
            len(outside), ', '.join('U+%04X' % cp for cp in outside[:16])))
    all_cps = sorted(records)
    preload = read_preload(preload_path)
    if len(set(preload)) != len(preload):
        raise G16Error(2, 'duplicate cp in the preload list')
    missing = [cp for cp in preload if cp not in records]
    if missing:
        raise G16Error(2, 'preload cp not in the bundle: %s' % ', '.join('U+%04X' % cp for cp in missing[:8]))

    rank = rank_index(all_cps)
    off_rank = HDR_LEN
    off_pre = off_rank + RANK_LEN
    pre_end = off_pre + len(preload) * PRE_REC
    off_all = (pre_end + SECTOR - 1) // SECTOR * SECTOR
    adjust, modified = head_identity(raw)
    header = HEADER.pack(MAGIC, VERSION, HDR_LEN, CELL_W, CELL_H, ASCENT, FLAG_BMP_ONLY,
                         len(all_cps), len(preload), len(raw), adjust, modified,
                         hashlib.sha256(raw).digest(), off_rank, RANK_LEN, zlib.crc32(rank),
                         off_pre, PRE_REC, off_all, ALL_REC)
    header += bytes(HDR_LEN - len(header))
    out = bytearray(header + rank)
    for cp in preload:
        out += struct.pack('<I', cp) + records[cp]
    out += bytes(off_all - len(out))
    for cp in all_cps:
        out += records[cp]
    return bytes(out), dict(n_all=len(all_cps), n_pre=len(preload), off_all=off_all, size=len(out))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--ttf', required=True)
    ap.add_argument('--preload', required=True, help='one hex code point per line (gen_preload16.py)')
    ap.add_argument('-o', '--output', required=True)
    args = ap.parse_args(argv)
    try:
        data, info = build(args.ttf, args.preload)
    except G16Error as e:
        sys.stderr.write('gen_g16: %s\n' % e)
        return e.rc
    tmp = args.output + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, args.output)
    print('gen_g16: %s n_all=%d n_pre=%d off_all=%d size=%d' % (
        args.output, info['n_all'], info['n_pre'], info['off_all'], info['size']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
