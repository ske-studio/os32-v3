#!/usr/bin/env python3
"""Independent P6/KCG1 pixel oracle. Coordinates are zero-based terminal cells.

RGB is supplied from observed OF colors, never inferred from palette indices.
P6 has packed rows; Image additionally accepts RGB buffers with padded pitch.
compare_text returns None on success, otherwise the first differing 8x16 cell,
including foreground counts and the number of unequal pixels (counts alone
cannot detect a displaced glyph). No terminal control sequences are interpreted.
"""
from dataclasses import dataclass
from pathlib import Path
import struct


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    pixels: bytes
    pitch: int

    def __post_init__(self):
        if (self.width <= 0 or self.height <= 0 or self.pitch < self.width * 3
                or len(self.pixels) != self.pitch * self.height):
            raise ValueError('invalid RGB geometry/pitch')

    def pixel(self, x, y):
        if not (0 <= x < self.width and 0 <= y < self.height):
            raise ValueError('pixel outside image')
        off = y * self.pitch + x * 3
        return self.pixels[off:off + 3]


def read_ppm(path):
    """Read an exact 8-bit P6 raster, allowing header whitespace/comments."""
    data = Path(path).read_bytes()
    pos = 0

    def token():
        nonlocal pos
        while pos < len(data):
            if data[pos:pos + 1] == b'#':
                end = data.find(b'\n', pos)
                if end < 0:
                    raise ValueError('unterminated PPM comment')
                pos = end + 1
            elif data[pos] in b' \t\r\n\v\f':
                pos += 1
            else:
                break
        start = pos
        while pos < len(data) and data[pos] not in b' \t\r\n\v\f#':
            pos += 1
        return data[start:pos]

    if token() != b'P6':
        raise ValueError('expected P6')
    width, height, maximum = int(token()), int(token()), int(token())
    if maximum != 255 or pos >= len(data) or data[pos] not in b' \t\r\n\v\f':
        raise ValueError('expected 8-bit P6 separator')
    # Consume only the separator: a raster may start with whitespace or #.
    pos += 2 if data[pos:pos + 2] == b'\r\n' else 1
    return Image(width, height, data[pos:], width * 3)


class Font:
    """Uncompressed KCG1 reader, independent of the C loader/generator."""
    def __init__(self, path):
        data = Path(path).read_bytes()
        size = 94 * 94 * 33 + 256 * 17
        if len(data) != 16 + size:
            raise ValueError('KCG1 length')
        if struct.unpack('<4sIII', data[:16]) != (b'KCG1', size, 0, 0):
            raise ValueError('KCG1 header (compressed fonts unsupported)')
        self.kanji = data[16:16 + 8836 * 32]
        off = 16 + 8836 * 32
        if any(v not in (0, 1) for v in data[off:off + 8836] + data[-256:]):
            raise ValueError('KCG1 fetched flags')
        self.kf = data[off:off + 8836]
        self.ank = data[off + 8836:-256]
        self.af = data[-256:]

    def glyph(self, char):
        if len(char) != 1:
            raise ValueError('expected one character')
        code = ord(char)
        if 0x20 <= code <= 0x7e or 0xff61 <= code <= 0xff9f:
            index = code if code < 128 else code - 0xff61 + 0xa1
            if not self.af[index]:
                raise ValueError('unfetched ANK')
            return 8, self.ank[index * 16:(index + 1) * 16]
        try:
            jis = char.encode('euc_jp')
        except UnicodeEncodeError as exc:
            raise ValueError('unsupported character') from exc
        if len(jis) != 2 or not all(0xa1 <= b <= 0xfe for b in jis):
            raise ValueError('not JIS X 0208')
        index = (jis[0] - 0xa1) * 94 + jis[1] - 0xa1
        if not self.kf[index]:
            raise ValueError('unfetched kanji')
        return 16, self.kanji[index * 32:(index + 1) * 32]


@dataclass(frozen=True)
class Mismatch:
    row: int
    column: int
    expected_pixels: int
    actual_pixels: int
    different_pixels: int


class Terminal:
    def __init__(self, image, font, fg, bg, top=64):
        self.image, self.font = image, font
        self.fg, self.bg = bytes(fg), bytes(bg)
        self.top = top  # mutation anchor: reserved scanlines
        if (len(self.fg) != 3 or len(self.bg) != 3 or self.fg == self.bg
                or not 0 <= top <= image.height):
            raise ValueError('invalid terminal colors/top')
        self.columns = image.width // 8
        self.rows = (image.height - top) // 16

    def compare_text(self, row, column, text, cursor=()):
        """cursor is an iterable of inverted (row,column) cells, including both
        halves of a wide cursor. Compare blank tails explicitly with spaces."""
        cells = []
        for char in text:
            width, bitmap = self.font.glyph(char)
            for half in range(width // 8):
                cells.append([bitmap[y * (width // 8) + half] for y in range(16)])
        if not (0 <= row < self.rows and 0 <= column <= self.columns
                and column + len(cells) <= self.columns):
            raise ValueError('text outside terminal')
        for index, mask in enumerate(cells):
            col = column + index
            expected = actual = different = 0
            invert = (row, col) in cursor  # mutation anchor: cursor composition
            for y, bits in enumerate(mask):
                for x in range(8):
                    on = bool(bits & (0x80 >> x)) ^ invert
                    wanted = self.fg if on else self.bg
                    got = self.image.pixel(col * 8 + x, self.top + row * 16 + y)
                    expected += on
                    actual += got == self.fg
                    different += got != wanted
            if different:
                return Mismatch(row, col, expected, actual, different)
        return None

    def cursor_positions(self, row, column, text):
        """Locate fully inverted cells relative to known underlying text.
        A partial cursor raises ValueError; normal cells are omitted."""
        result = []
        for char in text:
            width, _ = self.font.glyph(char)
            span = tuple((row, column + i) for i in range(width // 8))
            if self.compare_text(row, column, char) is None:
                pass
            elif self.compare_text(row, column, char, span) is None:
                result.extend(span)
            else:
                raise ValueError('neither glyph nor complete inverted cursor')
            column += width // 8
        return result

    def bottom_line(self, text, cursor=()):
        width = sum(self.font.glyph(c)[0] // 8 for c in text)
        return self.compare_text(self.rows - 1, 0,
                                 text + ' ' * (self.columns - width), cursor)

    def unexpected_color(self):
        """First (x,y,RGB) outside fg/bg, excluding the reserved stage band."""
        for y in range(self.top, self.image.height):
            for x in range(self.image.width):
                rgb = self.image.pixel(x, y)
                if rgb not in (self.fg, self.bg):
                    return x, y, rgb
        return None


def reserved_unchanged(before, after, top=64):
    if (before.width, before.height) != (after.width, after.height):
        return False
    if not 0 <= top <= before.height:
        raise ValueError('invalid reserved height')
    return all(before.pixel(x, y) == after.pixel(x, y)
               for y in range(top) for x in range(before.width))
