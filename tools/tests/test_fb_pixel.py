#!/usr/bin/env python3
"""Host-only independent raster fixtures and runtime mutations for fb_pixel."""
CHECK = dict(name='fb-pixel', tier=1, needs=[], budget=12,
    covers=['tools/ppc/fb_pixel.py', 'tools/tests/test_fb_pixel.py',
            'tools/gen_font16.py', 'tools/gen_g16.py',
            'assets/fonts/**', 'tools/fetch_fonts.py', 'requirements.txt'])

import argparse
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/ppc/fb_pixel.py'
FG, BG = b'\xf0\xe0\xd0', b'\x10\x20\x30'


def load(path):
    spec = importlib.util.spec_from_file_location('fb_pixel_fixture', path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def suite(m, fontpath, work):
    raw = fontpath.read_bytes()[16:]

    # Raster reference does not use Font.glyph or any oracle drawing logic.
    def glyph(ch):
        code = ord(ch)
        if code < 128 or 0xff61 <= code <= 0xff9f:
            code = code if code < 128 else code - 0xff61 + 0xa1
            return 8, raw[8836 * 33 + code * 16:8836 * 33 + (code + 1) * 16]
        a, b = ch.encode('euc_jp')
        index = (a - 0xa1) * 94 + b - 0xa1
        return 16, raw[index * 32:(index + 1) * 32]

    for width, height in ((800, 600), (640, 480)):
        pitch = width * 3 + 27
        buf = bytearray((BG * width + b'X' * 27) * height)
        for y in range(64):
            buf[y * pitch:y * pitch + width * 3] = b'\x66\x77\x88' * width

        def draw(row, col, text, cursor=(), cursor_top=0):
            for ch in text:
                wide, bitmap = glyph(ch)
                for y in range(16):
                    bits = int.from_bytes(bitmap[y * (wide // 8):(y + 1) * (wide // 8)], 'big')
                    for x in range(wide):
                        on = (bits >> (wide - 1 - x)) & 1
                        if (row, col + x // 8) in cursor and y >= cursor_top:
                            on ^= 1
                        off = (64 + row * 16 + y) * pitch + (col * 8 + x) * 3
                        buf[off:off + 3] = FG if on else BG
                col += wide // 8

        font = m.Font(fontpath)
        text = 'Ab9 日本語 ｱｲ!'
        draw(2, 3, text)
        draw(4, 0, 'A日 ', ((4, 1), (4, 2), (4, 3)))
        before = m.Image(width, height, bytes(buf), pitch)
        term = m.Terminal(before, font, FG, BG)
        assert term.compare_text(2, 3, text) is None, 'fixed text'
        assert term.compare_text(2, 4, text) is not None
        assert term.compare_text(3, 3, text) is not None
        assert term.cursor_positions(4, 0, 'A日 ') == [(4, 1), (4, 2), (4, 3)]
        assert term.unexpected_color() is None
        damaged = bytearray(buf)
        cursor_off = (64 + 4 * 16) * pitch + 3 * 8 * 3
        damaged[cursor_off:cursor_off + 3] = BG
        try:
            m.Terminal(m.Image(width, height, bytes(damaged), pitch), font, FG, BG).cursor_positions(4, 0, 'A日 ')
        except ValueError:
            pass
        else:
            raise AssertionError('partial cursor accepted')
        draw(6, 0, 'A日 ', ((6, 1), (6, 2), (6, 3)), cursor_top=14)
        underline = m.Terminal(m.Image(width, height, bytes(buf), pitch), font,
                               FG, BG, cursor_top=14)
        assert underline.cursor_positions(6, 0, 'A日 ') == [(6, 1), (6, 2), (6, 3)]
        assert underline.compare_text(6, 0, 'A日 ', ((6, 1), (6, 2), (6, 3))) is None
        assert m.Terminal(underline.image, font, FG, BG).compare_text(
            6, 0, 'A日 ', ((6, 1), (6, 2), (6, 3))) is not None
        # P6 strips framebuffer pitch padding; first raster byte may be whitespace.
        ppm = work / 'screen.ppm'
        packed = b''.join(buf[y * pitch:y * pitch + width * 3] for y in range(height))
        ppm.write_bytes(b'P6\n# synthetic padded source\n%d %d\n255\n' % (width, height) + packed)
        image = m.read_ppm(ppm)
        assert m.Terminal(image, font, FG, BG).compare_text(2, 3, text) is None
        for row in range(term.rows):
            draw(row, 0, ' ' * term.columns)
        assert m.Terminal(m.Image(width, height, bytes(buf), pitch), font, FG, BG).bottom_line('') is None
        # rows+2 numbered output lines, newline after each: final two rows scroll.
        lines = ['line %02d' % i for i in range(term.rows + 2)]
        visible = lines[-(term.rows - 1):] + ['']
        for row, line in enumerate(visible):
            draw(row, 0, line)
        after = m.Image(width, height, bytes(buf), pitch)
        scrolled = m.Terminal(after, font, FG, BG)
        for row, line in enumerate(visible):
            assert scrolled.compare_text(row, 0, line + ' ' * (term.columns - len(line))) is None
        assert scrolled.bottom_line('') is None
        assert m.reserved_unchanged(before, after)
        buf[0] ^= 1
        assert not m.reserved_unchanged(before, m.Image(width, height, bytes(buf), pitch))
        off = (height - 1) * pitch
        buf[off:off + 3] = b'\xff\0\xff'
        bad = m.Terminal(m.Image(width, height, bytes(buf), pitch), font, FG, BG)
        assert bad.unexpected_color() == (0, height - 1, b'\xff\0\xff')
        # Equal ink counts, different geometry must still report failure.
        draw(2, 3, 'A')
        buf[(64 + 2 * 16) * pitch + 3 * 8 * 3] ^= 1
        diff = m.Terminal(m.Image(width, height, bytes(buf), pitch), font, FG, BG).compare_text(2, 3, 'A')
        assert diff is not None and (diff.row, diff.column) == (2, 3) and diff.different_pixels
    for broken in (b'', fontpath.read_bytes()[:-1],
                   fontpath.read_bytes()[:8] + struct.pack('<I', 1) + fontpath.read_bytes()[12:]):
        path = work / 'bad.kcg'; path.write_bytes(broken)
        try:
            m.Font(path)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid KCG accepted')
    for raw_ppm in (b'P6\n1 1\n255\n\x20\x23\x0a', b'P6\r\n1 1\r\n255\r\n\x20\x23\x0a'):
        ppm.write_bytes(raw_ppm)
        assert m.read_ppm(ppm).pixel(0, 0) == b'\x20\x23\x0a'


MUTATIONS = (
    ('cursor-shape', 'self.cursor_top = cursor_top', 'self.cursor_top = 0'),
    ('colors', 'self.fg, self.bg = bytes(fg), bytes(bg)', 'self.fg, self.bg = bytes(bg), bytes(fg)'),
    ('top-zero', 'self.top = top  # mutation anchor: reserved scanlines', 'self.top = 0'),
    ('column', 'col = column + index', 'col = column + index + 1'),
    ('row', 'self.top + row * 16 + y', 'self.top + (row + 1) * 16 + y'),
    ('bit-order', '0x80 >> x', '1 << x'),
    ('cursor-missing', 'invert = (row, col) in cursor  # mutation anchor: cursor composition', 'invert = False'),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    ttf = ROOT / 'assets/fonts/JF-Dot-Shinonome16.ttf'
    if not ttf.is_file():
        print('SKIP fb-pixel: JF-Dot-Shinonome16.ttf unavailable')
        return 0
    with tempfile.TemporaryDirectory(prefix='fb-pixel-', dir=os.environ.get('TMPDIR')) as temp:
        work = Path(temp)
        fontpath = work / 'fixture.kcg'
        subprocess.run([sys.executable, '-B', str(ROOT / 'tools/gen_font16.py'),
                        '--ebdt', str(ttf), str(fontpath)], check=True)
        suite(load(SOURCE), fontpath, work)
        print('PASS fb-pixel: 800x600, 640x480, padded pitch, text/clear/cursor/scroll')
        if args.mutate:
            source = SOURCE.read_text()
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, (name, 'nonunique mutation anchor')
                mutant = work / 'mutant.py'
                mutant.write_text(source.replace(old, new))
                try:
                    suite(load(mutant), fontpath, work)
                except (AssertionError, ValueError):
                    print('RED fb-pixel:', name)
                else:
                    raise AssertionError('surviving mutation: ' + name)
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_fb_pixel.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
