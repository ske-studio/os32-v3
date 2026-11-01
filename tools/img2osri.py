#!/usr/bin/env python3
"""Convert a picture to the fixed-palette OSRI v1 format."""
import argparse
import json
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


def quantized_colors(image, count):
    q = image.quantize(colors=count, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    raw = q.getpalette()
    counts = q.getcolors()
    merged = {}
    for n, i in counts:
        color = rgb444(raw[3 * i:3 * i + 3])
        merged[color] = merged.get(color, 0) + n
    return sorted(merged, key=lambda c: (-merged[c], c))


def palette_history(source, mode, events_limit, per_line, band, fixed_colors):
    if mode == 'fixed':
        colors = quantized_colors(source, 16)
        return (colors + [0] * 16)[:16], [], [colors + [0] * (16 - len(colors))] * H
    fixed = quantized_colors(source, 16)[:fixed_colors] if mode == 'B' else []
    first = quantized_colors(source.crop((0, 0, W, band)), 16)
    initial = fixed + [c for c in first if c not in fixed]
    initial = (initial + [0] * 16)[:16]
    current = initial.copy()
    history = []
    events = []
    last_line = -2
    for y in range(0, H, band):
        strip = source.crop((0, y, W, min(y + band, H)))
        if y and len(events) < events_limit and y - last_line >= 2:
            histogram = strip.getcolors(W * band)
            candidates = sorted(quantized_colors(strip, 16))
            changes = []
            for unused in range(min(per_line, events_limit - len(events))):
                palette = [expand(c) for c in current]
                best = None
                distances = [[sum((rgb[k] - c[k]) ** 2 for k in range(3)) for c in palette]
                             for _, rgb in histogram]
                candidate_distances = {candidate: [sum((rgb[k] - expand(candidate)[k]) ** 2
                                                       for k in range(3)) for _, rgb in histogram]
                                       for candidate in candidates}
                for index in range(len(fixed), 16):
                    if any(i == index for i, _ in changes):
                        continue
                    baseline = [min(row) for row in distances]
                    without = [min(row[:index] + row[index + 1:]) for row in distances]
                    for candidate in candidates:
                        if candidate == current[index]:
                            continue
                        delta = sum(histogram[j][0] * (min(without[j], distance) - baseline[j])
                                    for j, distance in enumerate(candidate_distances[candidate]))
                        choice = (delta, index, candidate)
                        if best is None or choice < best:
                            best = choice
                if best is None or best[0] >= 0:
                    break
                _, index, candidate = best
                current[index] = candidate
                changes.append((index, candidate))
            for index, color in sorted(changes):
                events.append((y, index, color))
                last_line = y
        history.extend([current.copy()] * min(band, H - y))
    return initial, events, history


def convert(source, mode='fixed', events_limit=100, per_line=1, band=4, fixed_colors=8):
    colors, events, history = palette_history(source, mode, events_limit, per_line, band, fixed_colors)
    src = source.load()
    planes = bytearray(PLANE * 4)
    error = [[0, 0, 0] for _ in range(W + 2)]
    for y in range(H):
        palette = [expand(c) for c in history[y]]
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
    size = EVENTS_OFF + 8 * len(events)
    header = struct.pack('<4sHHHHBBHIIIIIIHHHHIIII', b'OSRI', 1, 64, W, H, 4, 1, 0,
                         size, 64, PIXELS_OFF, PLANE * 4, EVENTS_OFF, len(events),
                         8, 0, band if events else 4, 0, 0, 0, 0, 0)
    entries = b''.join(struct.pack('<HBBHH', y, i, 0, c, 0) for y, i, c in events)
    data = bytearray(header + struct.pack('<16H', *colors) + planes + entries)
    crc = zlib.crc32(data)
    struct.pack_into('<I', data, 48, crc)
    return data, crc


def preview(path, source, out):
    data = open(path, 'rb').read()
    pal = [expand(c) for c in struct.unpack_from('<16H', data, 64)]
    count = struct.unpack_from('<I', data, 36)[0]
    events = [struct.unpack_from('<HBBHH', data, EVENTS_OFF + i * 8) for i in range(count)]
    event_index = 0
    image = Image.new('RGB', (W, H))
    pix = image.load()
    for y in range(H):
        while event_index < count and events[event_index][0] == y:
            _, index, _, color, _ = events[event_index]
            pal[index] = expand(color)
            event_index += 1
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
    draw.text((4, 3), 'OSRI', fill='white')
    draw.text((W + 4, 3), '256 colors', fill='white')
    canvas.save(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('--mode', choices=['fixed', 'A', 'B', 'C'], default='B')
    parser.add_argument('--events', type=int, default=100)
    parser.add_argument('--per-line', type=int, default=1)
    parser.add_argument('--band', type=int, choices=[4], default=4)
    parser.add_argument('--fixed-colors', type=int, default=8)
    parser.add_argument('--budget', help='JSON file with events and per_line limits')
    parser.add_argument('--dither', choices=['fs'], default='fs')
    parser.add_argument('--preview', required=True)
    args = parser.parse_args()
    if not 0 <= args.events <= 100 or args.per_line != 1 or not 0 <= args.fixed_colors <= 16:
        parser.error('invalid event, per-line, or fixed-color limit')
    if args.budget:
        with open(args.budget) as f:
            budget = json.load(f)
        args.events = min(args.events, budget['events'])
        args.per_line = min(args.per_line, budget['per_line'])
        if not 0 <= args.events <= 100 or args.per_line != 1:
            parser.error('invalid budget')
    source = resize_image(args.input)
    data, crc = convert(source, args.mode, args.events, args.per_line, args.band, args.fixed_colors)
    with open(args.output, 'wb') as f:
        f.write(data)
    preview(args.output, source, args.preview)
    count = struct.unpack_from('<I', data, 36)[0]
    lines = [struct.unpack_from('<H', data, EVENTS_OFF + i * 8)[0] for i in range(count)]
    max_line = max((lines.count(y) for y in set(lines)), default=0)
    print(f'events={count} max_per_line={max_line} budget={args.events}/{args.per_line} crc32={crc:08x} Pillow={pillow_version} mode={args.mode} band={args.band} fixed_colors={args.fixed_colors} dither={args.dither}')


if __name__ == '__main__':
    main()
