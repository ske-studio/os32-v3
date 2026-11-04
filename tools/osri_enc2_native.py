"""File boundary to the C11 host core; A/B and OSRI remain Python-owned.

Build explicitly with `make osri-enc2-core` before using the encoder. The S1
plan/quantize functions remain independent reference implementations.
"""
from pathlib import Path
import os
import resource
import struct
import subprocess
import tempfile
import time
import numpy as np
from osri_profile import OSRI_DEFAULT_OUT_BUDGET, OSRI_COMPARE_OUT_LIMIT
from osri_enc2_spec import Config, codes, components
from osri_enc2_plan import prepare
from osri_enc2_tree import row_candidates

ROOT = Path(__file__).resolve().parents[1]


def _u32(values):
    return np.asarray(values, dtype='<u4').tobytes()


def _run(mode, pixels, budget, cfg, payload, *, timed=True, dither=True, trace_path=None, threads=None):
    if threads is not None and (not isinstance(threads, int) or threads < 1):
        raise ValueError('threads must be a positive integer')
    pixels = np.asarray(pixels)
    if (pixels.ndim != 3 or pixels.shape[2] != 3 or
            not 1 <= pixels.shape[0] <= 400 or not 1 <= pixels.shape[1] <= 640 or
            np.any(pixels < 0) or np.any(pixels > 255)):
        raise ValueError('expected RGB8 image, at most 640x400')
    if not 0 <= budget <= OSRI_COMPARE_OUT_LIMIT:
        raise ValueError(f'OUT budget must be 0..{OSRI_COMPARE_OUT_LIMIT}')
    binary = Path(os.environ.get('ENC2_CORE', ROOT / 'build/out/osri-enc2-core'))
    if not binary.is_file():
        raise FileNotFoundError(f'{binary}: run make osri-enc2-core first')
    header = struct.pack('<15i', 0x43324345, 1, mode, *pixels.shape[:2], budget,
                         cfg.lookahead_rows, cfg.row_iters, cfg.round_trips,
                         cfg.row_moves, cfg.order_passes, cfg.start_delay_ns,
                         cfg.pixel0_offset_ns, timed, dither)
    with tempfile.TemporaryDirectory(prefix='enc2-core-') as name:
        request, response = Path(name) / 'input', Path(name) / 'output'
        request.write_bytes(header + pixels.astype(np.uint8).tobytes() + payload)
        command = [str(binary), str(request), str(response)]
        if trace_path is not None:
            command.append(str(trace_path))
        if threads is not None:
            command.extend(['--threads', str(threads)])
        subprocess.run(command, check=True)
        result = response.read_bytes()
    if result[:12] != struct.pack('<3I', 0x52324345, 1, mode):
        raise ValueError('invalid enc2 core response')
    return result[12:]


def plan(pixels, budget, cfg=Config(), prepared=None, active_rows=None, *, trace_path=None, threads=None):
    pixels = np.asarray(pixels, dtype=np.int32)
    colors, roots, stats = prepared if prepared is not None else prepare(pixels, cfg)
    stats = dict(stats)
    start = time.perf_counter()
    height = len(pixels)
    active = set(range(1, height)) if active_rows is None else set(active_rows)
    if any(y <= 0 or y >= height for y in active):
        raise ValueError('active rows must be between 1 and H-1')
    candidates = {y: row_candidates(pixels, y, roots, cfg) for y in sorted(active | {0})}
    stats['candidate_seconds'] = time.perf_counter() - start
    payload = _u32([len(colors)]) + _u32(codes(colors)) + _u32(codes([n.color for n in roots]))
    payload += bytes(int(y in active) for y in range(height))
    for y in range(height):
        q = codes(candidates[y]) if y in candidates else []
        payload += _u32([len(q)]) + _u32(q)
    response = _run(0, pixels, budget, cfg, payload, trace_path=trace_path, threads=threads)
    offset = 0

    def take(fmt):
        nonlocal offset
        values = struct.unpack_from(fmt, response, offset)
        offset += struct.calcsize(fmt)
        return values

    palettes = components(np.array(take(f'<{height * 16}I')).reshape(height, 16))
    orders = [tuple(take(f'<{take("<I")[0]}I')) for _ in range(height)]
    trace = list(take(f'<{take("<I")[0]}q'))
    visits = [take('<Iqq') for _ in range(take('<I')[0])]
    evaluations, forward, sweep = take('<3Q')
    if offset != len(response):
        raise ValueError('trailing enc2 core response')
    stats.update(forward_seconds=forward / 1e9, sweep_seconds=sweep / 1e9,
                 evaluations=evaluations, j_trace=trace, visits=visits,
                 active_rows=sorted(active), backend='C11',
                 core_RSS_KiB=resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)
    return palettes, orders, stats


def quantize(pixels, palettes, orders, cfg=Config(), *, timed=True, dither=True):
    palettes = np.asarray(palettes)
    if (palettes.shape != (len(pixels), 16, 3) or np.any(palettes < 0) or
            np.any(palettes > 15) or len(orders) != len(pixels)):
        raise ValueError('invalid palette or order dimensions')
    payload = _u32(codes(palettes))
    for order in orders:
        payload += _u32([len(order), *order])
    result = _run(1, pixels, OSRI_COMPARE_OUT_LIMIT, cfg, payload, timed=timed, dither=dither)
    return np.frombuffer(result, dtype=np.uint8).reshape(np.shape(pixels)[:2]).copy()


def encode_image(source, output, *, budget=OSRI_DEFAULT_OUT_BUDGET, threads=None):
    """Convert one image to enc2 OSRI with black padding and the shared budget."""
    from img2osri import resize_image
    from osri_enc2_pack import pack

    start = time.perf_counter()
    pixels = np.asarray(resize_image(source), dtype=np.int32)
    palettes, orders, stats = plan(pixels, budget, threads=threads)
    indices = quantize(pixels, palettes, orders)
    data = pack(palettes, orders, indices, budget)
    Path(output).write_bytes(data)
    return dict(stats, seconds=time.perf_counter() - start,
                CRC=f'{struct.unpack_from("<I", data, 48)[0]:08x}')


def main():
    import argparse
    import json

    parser = argparse.ArgumentParser(description='Convert one image to enc2 B=31 OSRI')
    parser.add_argument('input')
    parser.add_argument('output')
    parser.add_argument('--threads', type=int, default=None,
                        help='worker count (default: logical CPUs; 1: serial)')
    args = parser.parse_args()
    if args.threads is not None and args.threads < 1:
        parser.error('--threads must be positive')
    print(json.dumps(encode_image(args.input, args.output, threads=args.threads)), flush=True)


if __name__ == '__main__':
    main()
