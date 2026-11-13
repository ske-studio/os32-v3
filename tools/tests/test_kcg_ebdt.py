#!/usr/bin/env python3
"""Verify every KCG cell against the EBDT bitmap and the PPC root manifest."""
CHECK = dict(name='kcg-ebdt', tier=1, needs=['make:all', 'make:assets/fonts/shino16.kcgfont', 'make:unicode_bin'], budget=60,
             covers=['tools/gen_font16.py', 'tools/gen_g16.py', 'tools/mkroot_image.py',
                     'build/ram-root.mk', 'build/ppc-kboot.mk', 'build/assets.mk', 'build/font-fetch.mk',
                     'assets/fonts/**', 'tools/fetch_fonts.py',
                     'tools/gen_unicode.c', 'lib/unicode_jis_table.h', 'kernel/console/unicode_file.h',
                     'include/endian_le.h', 'include/types.h', 'include/callconv.h',
                     'build/ppc-ram-root.json', 'include/config.h',
                     'tools/tests/test_kcg_ebdt.py', 'tools/tests/test_mkroot_image.py',
                     'build/core.yaml', 'build/packages.yaml', 'userland/deploy.yaml',
                     'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/mkfat12.py'])
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / 'assets/fonts/JF-Dot-Shinonome16.ttf'
sys.path.insert(0, str(ROOT / 'tools'))
import gen_font16


def check():
    data = gen_font16.build_ebdt(FONT)
    assert len(data) == 295956 and struct.unpack_from('<4sIII', data) == (b'KCG1', 295940, 0, 0)
    f = TTFont(FONT)
    strike = next(i for i, s in enumerate(f['EBLC'].strikes)
                  if (s.bitmapSizeTable.ppemX, s.bitmapSizeTable.ppemY, s.bitmapSizeTable.bitDepth) == (16, 16, 1))
    metrics = {}
    for sub in f['EBLC'].strikes[strike].indexSubTables:
        for gid in range(sub.firstGlyphIndex, sub.lastGlyphIndex + 1):
            metrics[gid] = getattr(sub, 'metrics', None)
    cmap = f.getBestCmap()
    order = f.getGlyphOrder()
    bitmaps = f['EBDT'].strikeData[strike]
    def expected(ch, width):
        out = bytearray(width * 2)
        if ch is None:
            return out
        gid = f.getGlyphID(cmap[ord(ch)]) if ord(ch) in cmap else 0
        if gid not in metrics or order[gid] not in bitmaps:
            return out
        g = bitmaps[order[gid]]
        m = metrics[gid] or g.metrics
        for y in range(m.height):
            for x in range(m.width):
                bit = y * m.width + x
                if g.imageData[bit // 8] & (128 >> (bit % 8)):
                    px = m.horiBearingX + x - (4 if gid == 0 and width == 8 else 0)
                    py = 14 - m.horiBearingY + y
                    assert 0 <= px < width and 0 <= py < 16
                    out[py * (width // 8) + px // 8] |= 128 >> (px % 8)
        return out
    pos = 16
    for hi in range(0x21, 0x7f):
        for lo in range(0x21, 0x7f):
            try:
                ch = bytes([hi | 128, lo | 128]).decode('euc_jp')
            except UnicodeDecodeError:
                ch = None
            assert data[pos:pos + 32] == expected(ch, 16), (hi, lo)
            pos += 32
    assert data[pos:pos + 8836] == bytes([1]) * 8836
    pos += 8836
    for code in range(256):
        ch = chr(code) if 32 <= code <= 126 else chr(0xff61 + code - 0xa1) if 0xa1 <= code <= 0xdf else None
        assert data[pos:pos + 16] == expected(ch, 8), code
        pos += 16
    assert data[pos:] == bytes([1]) * 256
    # A font with no embedded strike must fail rather than rasterize outlines.
    from fontTools.ttLib import TTFont as Font
    from gen_g16 import strike_cells, G16Error
    empty = Font()
    try:
        strike_cells(empty)
    except G16Error as exc:
        assert exc.rc == 3
    else:
        raise AssertionError('missing EBDT accepted')
    manifest = json.loads((ROOT / 'build/ppc-ram-root.json').read_text())
    assert set(manifest) == {'/fonts/shino16.kcg', '/fonts/shino16.txt', '/sys/unicode.bin'}
    config = (ROOT / 'include/config.h').read_text()
    assert all(path in config for path in manifest)
    assert (ROOT / manifest['/fonts/shino16.kcg']).read_bytes() == data
    from test_mkroot_image import entries
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        tmp = Path(tmp)
        motd = tmp / 'motd'
        motd.write_text('OS32 root\n')
        iso = tmp / 'root.iso'
        subprocess.run([sys.executable, '-B', str(ROOT / 'tools/mkroot_image.py'),
                        '--motd', str(motd), '--manifest', str(ROOT / 'build/ppc-ram-root.json'),
                        '--out', str(iso)], cwd=ROOT, check=True, capture_output=True)
        image = iso.read_bytes()
        root = entries(image, image[16 * 2048 + 156:])
        for guest, host in manifest.items():
            directory, name = guest.strip('/').upper().split('/')
            record = entries(image, root[directory])[name]
            off = int.from_bytes(record[2:6], 'little') * 2048
            size = int.from_bytes(record[10:14], 'little')
            assert image[off:off + size] == (ROOT / host).read_bytes()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    check()
    if args.mutate:
        source = (ROOT / 'tools/gen_font16.py').read_text()
        anchor = 'if 0x20 <= code <= 0x7E:'
        assert source.count(anchor) == 1
        mutant = source.replace(anchor, 'if code == 0x41: return None\n    ' + anchor)
        namespace = {'__name__': 'kcg_mutant'}
        exec(compile(mutant, 'gen_font16_mutant.py', 'exec'), namespace)
        gen_font16.ank_to_unicode = namespace['ank_to_unicode']
        try:
            check()
        except AssertionError:
            print('mutation ANK mapping: RED')
        else:
            raise AssertionError('mutation survived')
    print('kcg-ebdt OK')

def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
