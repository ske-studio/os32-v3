#!/usr/bin/env python3
"""Convert one picture to OSRI v2 (default: enc2, shared OUT budget)."""
import argparse
import struct
import zlib

from osri_profile import OSRI_DEFAULT_OUT_BUDGET
from osri_enc2_spec import BUDGETS

import numpy as np
from PIL import Image, __version__ as pillow_version

W, H = 640, 400
PLANE = W * H // 8
PIXELS_OFF, EVENTS_OFF = 96, 128096
MAX_EVENTS = 1197
PALETTE_COLORS = 16
OBS_MAX_EVENTS = (H - 1) * PALETTE_COLORS
OBS_OUTS_PER_COLOR = 4  # Index, G, R, B; the first index is preselected.
MASKS = np.array([[bool(m & (1 << c)) for c in range(3)] for m in range(1, 8)])


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


def quantized_colors(image):
    """Used MEDIANCUT colors, rounded to RGB444, sorted and black padded."""
    q = image.quantize(colors=16, method=Image.Quantize.MEDIANCUT,
                       dither=Image.Dither.NONE)
    raw = q.getpalette()
    colors = sorted({rgb444(raw[3 * i:3 * i + 3]) for _, i in q.getcolors()})
    return colors + [0] * (16 - len(colors))


def distances(pixels, palette):
    return np.sum((pixels[:, None, :].astype(np.int32) -
                   palette[None, :, :].astype(np.int32)) ** 2, axis=2)


def out_count(old, new):
    diff = old != new
    slots = int(np.any(diff, axis=1).sum())
    return int(diff.sum()) + max(0, slots - 1)


def optimize_row(old, pixels, endpoints, budget, band=1):
    """Exact final OUT accounting, greedy gain per extra OUT.

    Later refinements of a slot are merged before serialization, not executed
    as intermediate writes. The first selected slot is preselected in active
    display; all other index writes occur inside the same HBLANK.
    """
    p = old.copy()
    pts = pixels.reshape(-1, 3).astype(np.int32)
    weights = np.ones(len(pts), dtype=np.float64)
    weights[band * W:] = 0.5
    # Per-component lookup avoids a large three-channel candidate tensor.
    component = [(pts[:, c, None] - np.arange(16)[None, :] * 17) ** 2
                 for c in range(3)]
    candidates = np.unique(endpoints.reshape(-1, 3), axis=0)
    used = 0
    for _ in range(2 * budget):
        dp = distances(pts, p * 17).T
        baseline = dp.min(axis=0)
        base_loss = float(np.sum(baseline * weights))
        best = None
        for slot in range(16):
            proposal = np.where(MASKS[:, None, :], candidates[None, :, :], p[slot])
            proposal = np.unique(proposal.reshape(-1, 3), axis=0)
            changed = np.count_nonzero(proposal != old[slot], axis=1)
            old_diff = np.count_nonzero(p != old, axis=1)
            components_other = int(old_diff.sum() - old_diff[slot])
            slots_other = int(np.count_nonzero(old_diff) - (old_diff[slot] > 0))
            costs = components_other + changed + np.maximum(0, slots_other + (changed > 0) - 1)
            valid = (costs <= budget) & np.any(proposal != p[slot], axis=1)
            proposal, costs = proposal[valid], costs[valid]
            if not len(proposal):
                continue
            without = np.min(np.delete(dp, slot, axis=0), axis=0)
            dc = sum(component[c][:, proposal[:, c]].T for c in range(3))
            loss = np.sum(np.minimum(dc, without[None, :]) * weights, axis=1)
            gain = base_loss - loss
            scores = gain / np.maximum(1, costs - used)
            k = int(np.argmax(scores))
            if gain[k] <= 1e-7:
                continue
            key = (float(scores[k]), float(gain[k]), -int(costs[k]), -slot,
                   tuple(-int(v) for v in proposal[k]))
            if best is None or key > best[0]:
                best = (key, slot, proposal[k], int(costs[k]))
        if best is None:
            break
        _, slot, value, used = best
        p[slot] = value
    assert out_count(old, p) <= budget
    return p


def palette_history(source, hblank_outs=5, band=1):
    if source.size != (W, H) or source.mode != 'RGB':
        raise ValueError('source must be 640x400 RGB')
    if hblank_outs not in (5, 6) or band not in (1, 2, 4):
        raise ValueError('invalid HBLANK budget or band')
    pixels = np.asarray(source)
    endpoints = np.array([[[c >> 8, c >> 4 & 15, c & 15]
                           for c in quantized_colors(source.crop((0, y, W, y + band)))]
                          for y in range(0, H, band)], dtype=np.uint8)
    current = endpoints[0].copy()
    initial = [(int(r) << 8) | (int(g) << 4) | int(b) for r, g, b in current]
    history = [initial.copy() for _ in range(band)]
    events = []
    for y in range(band, H, band):
        planned = optimize_row(current, pixels[y:y + 2 * band],
                               endpoints[y // band:y // band + 2], hblank_outs, band)
        colors = [(int(r) << 8) | (int(g) << 4) | int(b) for r, g, b in planned]
        for index in range(16):
            if np.any(current[index] != planned[index]):
                color = colors[index]
                events.append((y, index, color))
        history.extend([colors.copy() for _ in range(band)])
        current = planned
    assert len(events) <= min(MAX_EVENTS, 3 * (H // band - 1))
    return initial, events, history


def obs_quantized_colors(image):
    """Keep candidate slots even when RGB444 rounding makes colors equal."""
    q = image.quantize(colors=PALETTE_COLORS, method=Image.Quantize.MEDIANCUT,
                       dither=Image.Dither.NONE)
    raw = q.getpalette()
    colors = sorted(rgb444(raw[3 * i:3 * i + 3]) for _, i in q.getcolors())
    return colors + [0] * (PALETTE_COLORS - len(colors))


def obs_optimize_row(old, pixels, candidates, colors):
    """Select exactly colors distinct slots and candidate slots by row SSE."""
    dp = distances(pixels, np.array([expand(c) for c in old])).T
    dc = distances(pixels, np.array([expand(c) for c in candidates])).T
    remaining = sorted(range(PALETTE_COLORS), key=lambda i: (candidates[i], i))
    available = list(range(PALETTE_COLORS))
    planned, selected = old.copy(), []
    for _ in range(colors):
        # Minimizing the new SSE maximizes its reduction from the same baseline.
        nearest = dp.argmin(axis=0)
        first, second = np.partition(dp, 1, axis=0)[:2]
        best = None
        for slot in available:
            without = np.where(nearest == slot, second, first)
            losses = np.minimum(dc[remaining], without).sum(axis=1)
            k = int(losses.argmin())
            candidate = remaining[k]
            key = (int(losses[k]), slot, candidates[candidate], candidate)
            if best is None or key < best[0]:
                best = (key, slot, candidate)
        _, slot, candidate = best
        planned[slot] = candidates[candidate]
        selected.append(slot)
        dp[slot] = dc[candidate]
        available.remove(slot)
        remaining.remove(candidate)
    return planned, selected


def obs_palette_history(source, colors):
    if source.size != (W, H) or source.mode != 'RGB':
        raise ValueError('source must be 640x400 RGB')
    if not 1 <= colors <= PALETTE_COLORS:
        raise ValueError('invalid observation color count')
    pixels = np.asarray(source)
    rows = [obs_quantized_colors(source.crop((0, y, W, y + 1))) for y in range(H)]
    initial = rows[0].copy()
    history, events = [initial.copy()], []
    for y in range(1, H):
        planned, selected = obs_optimize_row(history[-1], pixels[y], rows[y], colors)
        for index in sorted(selected):
            events.append((y, index, planned[index]))
        history.append(planned)
    assert len(events) == (H - 1) * colors <= OBS_MAX_EVENTS
    return initial, events, history


def convert(source, hblank_outs=5, band=1, *, obs_colors=None):
    if obs_colors is None:
        colors, events, history = palette_history(source, hblank_outs, band)
    else:
        if band != 1:
            raise ValueError('observation requires band=1')
        colors, events, history = obs_palette_history(source, obs_colors)
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
    header = struct.pack('<4sHHHHBBHIIIIIIHHHHIIII', b'OSRI', 2, 64, W, H, 4, 1, 0,
                         size, 64, PIXELS_OFF, PLANE * 4, EVENTS_OFF, len(events),
                         8, 0, band, 0, 0, 0, 0, 0)
    entries = b''.join(struct.pack('<HBBHH', y, i, 0, c, 0) for y, i, c in events)
    data = bytearray(header + struct.pack('<16H', *colors) + planes + entries)
    crc = zlib.crc32(data)
    struct.pack_into('<I', data, 48, crc)
    return data, crc


def obs_statistics(data, colors):
    palette = list(struct.unpack_from('<16H', data, 64))
    changed = [0] * (H - 1)
    for offset in range(EVENTS_OFF, len(data), 8):
        y, index, _, color, _ = struct.unpack_from('<HBBHH', data, offset)
        changed[y - 1] += palette[index] != color
        palette[index] = color
    return dict(bytes=len(data), events=(len(data) - EVENTS_OFF) // 8,
                written_colors=colors, actual_changed_colors_min=min(changed),
                actual_changed_colors_max=max(changed),
                hblank_outs=OBS_OUTS_PER_COLOR * colors - 1,
                total_outs=OBS_OUTS_PER_COLOR * colors,
                crc32=f'{struct.unpack_from("<I", data, 48)[0]:08x}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('--mode', choices=['PL-B'], default='PL-B')
    parser.add_argument('--hblank-outs', type=int, choices=[5, 6])
    parser.add_argument('--obs-colors', type=int, choices=range(1, PALETTE_COLORS + 1))
    parser.add_argument('--encoder', choices=['pl-b', 'enc2'])
    parser.add_argument('--outs', type=int, choices=BUDGETS)
    parser.add_argument('--obs-outs', type=int, choices=BUDGETS)
    parser.add_argument('--threads', type=int,
                        help='worker count (default: logical CPUs; 1: serial)')
    parser.add_argument('--band', type=int, choices=[1, 2, 4], default=1)
    parser.add_argument('--dither', choices=['fs'], default='fs')
    args = parser.parse_args()
    if args.outs is not None and args.obs_outs is not None:
        parser.error('--outs and --obs-outs cannot be combined')
    if args.threads is not None and args.threads < 1:
        parser.error('--threads must be positive')
    if args.encoder is None:
        args.encoder = ('pl-b' if args.hblank_outs is not None or args.obs_colors is not None
                        else 'enc2')
    if args.encoder == 'enc2':
        if args.hblank_outs is not None or args.obs_colors is not None or args.band != 1:
            parser.error('enc2 requires --band 1; cannot use --hblank-outs/--obs-colors')
        from osri_enc2_native import encode_image
        budget = args.outs if args.outs is not None else args.obs_outs
        if budget is None:
            budget = OSRI_DEFAULT_OUT_BUDGET
        stats = encode_image(args.input, args.output, budget=budget, threads=args.threads)
        print(f'encoder=enc2 B={budget} crc32={stats["CRC"]}')
        return
    if args.outs is not None or args.obs_outs is not None or args.threads is not None:
        parser.error('--outs/--obs-outs/--threads require --encoder enc2')
    if args.obs_colors is not None and (args.hblank_outs is not None or args.band != 1):
        parser.error('--obs-colors requires --band 1 and cannot use --hblank-outs')
    if args.hblank_outs is None:
        args.hblank_outs = 5
    source = resize_image(args.input)
    data, crc = convert(source, args.hblank_outs, args.band, obs_colors=args.obs_colors)
    with open(args.output, 'wb') as f:
        f.write(data)
    count = struct.unpack_from('<I', data, 36)[0]
    palette = list(struct.unpack_from('<16H', data, 64))
    row_costs = {}
    for offset in range(EVENTS_OFF, len(data), 8):
        y, index, _, color, _ = struct.unpack_from('<HBBHH', data, offset)
        components = sum(((palette[index] >> shift) & 15) != ((color >> shift) & 15)
                         for shift in (8, 4, 0))
        row_costs[y] = row_costs[y] + components + 1 if y in row_costs else components
        palette[index] = color
    if args.obs_colors is None:
        print(f'events={count} max_outs={max(row_costs.values(), default=0)} '
              f'N={args.hblank_outs} crc32={crc:08x} Pillow={pillow_version} args={vars(args)}')
    else:
        stats = obs_statistics(data, args.obs_colors)
        print(' '.join(f'{key}={value}' for key, value in stats.items()) +
              f' Pillow={pillow_version} NumPy={np.__version__} args={vars(args)}')


if __name__ == '__main__':
    main()
