#!/usr/bin/env python3
"""Convert a picture to the fixed-palette OSRI v1 format."""
import argparse
import struct
import zlib

from PIL import Image, ImageDraw, __version__ as pillow_version

W, H = 640, 400
PLANE = W * H // 8
PIXELS_OFF, EVENTS_OFF = 96, 128096


def resize_image(path):
    with Image.open(path) as source:
        source = source.convert('RGBA')
        black = Image.new('RGBA', source.size, (0, 0, 0, 255))
        black.alpha_composite(source)
        source = black.convert('RGB')
        source.thumbnail((W, H), Image.Resampling.LANCZOS)
        result = Image.new('RGB', (W, H))
        result.paste(source, ((W - source.width) // 2, (H - source.height) // 2))
        return result


def rgb444(rgb):
    r, g, b = ((v * 15 + 127) // 255 for v in rgb)
    return (r << 8) | (g << 4) | b


def expand(c):
    return ((c >> 8 & 15) * 17, (c >> 4 & 15) * 17, (c & 15) * 17)


def trunc16(n):
    return n // 16 if n >= 0 else -((-n) // 16)


def convert(source):
    q = source.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    raw = q.getpalette()
    colors = []
    for i in range(len(raw) // 3):
        color = rgb444(raw[i * 3:i * 3 + 3])
        if color not in colors:
            colors.append(color)
    colors.extend([0] * (16 - len(colors)))
    palette = [expand(c) for c in colors]
    src = source.load()
    planes = bytearray(PLANE * 4)
    error = [[0, 0, 0] for _ in range(W + 2)]
    for y in range(H):
        nxt = [[0, 0, 0] for _ in range(W + 2)]
        for x in range(W):
            rgb = [max(0, min(255, src[x, y][c] + error[x + 1][c])) for c in range(3)]
            index = min(range(16), key=lambda i: sum((rgb[c] - palette[i][c]) ** 2 for c in range(3)))
            offset = y * (W // 8) + x // 8
            for p in range(4):
                if index & (1 << p):
                    planes[p * PLANE + offset] |= 0x80 >> (x & 7)
            for c in range(3):
                d = rgb[c] - palette[index][c]
                error[x + 2][c] += trunc16(d * 7)
                nxt[x][c] += trunc16(d * 3)
                nxt[x + 1][c] += trunc16(d * 5)
                nxt[x + 2][c] += trunc16(d)
        error = nxt
    header = struct.pack('<4sHHHHBBHIIIIIIHHHHIIII', b'OSRI', 1, 64, W, H, 4, 1, 0,
                         EVENTS_OFF, 64, PIXELS_OFF, PLANE * 4, EVENTS_OFF, 0,
                         8, 0, 4, 0, 0, 0, 0, 0)
    data = bytearray(header + struct.pack('<16H', *colors) + planes)
    crc = zlib.crc32(data)
    struct.pack_into('<I', data, 48, crc)
    return data, crc


def preview(path, source, out):
    data = open(path, 'rb').read()
    pal = [expand(c) for c in struct.unpack_from('<16H', data, 64)]
    image = Image.new('RGB', (W, H))
    pix = image.load()
    for y in range(H):
        for x in range(W):
            off = PIXELS_OFF + y * (W // 8) + x // 8
            mask = 0x80 >> (x & 7)
            idx = sum((1 << p) for p in range(4) if data[off + p * PLANE] & mask)
            pix[x, y] = pal[idx]
    reference_palette = source.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    reference = source.quantize(palette=reference_palette, dither=Image.Dither.FLOYDSTEINBERG).convert('RGB')
    canvas = Image.new('RGB', (2 * W, H + 20), 'black')
    canvas.paste(image, (0, 20)); canvas.paste(reference, (W, 20))
    draw = ImageDraw.Draw(canvas)
    draw.text((4, 3), 'OSRI fixed 16', fill='white')
    draw.text((W + 4, 3), '256 colors', fill='white')
    canvas.save(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('--mode', choices=['fixed'], default='fixed')
    parser.add_argument('--dither', choices=['fs'], default='fs')
    parser.add_argument('--preview', required=True)
    args = parser.parse_args()
    source = resize_image(args.input)
    data, crc = convert(source)
    with open(args.output, 'wb') as f:
        f.write(data)
    preview(args.output, source, args.preview)
    print(f'events=0 max_per_line=0 budget=0 crc32={crc:08x} Pillow={pillow_version} mode={args.mode} dither={args.dither}')


if __name__ == '__main__':
    main()
