"""Explicit enc2 host experiments. S1 can limit planned rows; full runs belong to S3.

Example: python3 -B tools/osri_enc2_compare.py photo.png --output-dir OUT
         --row-start 198 --row-count 1
The table labels a partial search, including its exact row range and settings.
The PNG is always a single B-ascending, vertically stacked timing-model image.
"""
import argparse
import csv
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import resource
import struct
import time
import numpy as np
from PIL import Image, __version__ as pillow_version
from img2osri import resize_image, quantized_colors, optimize_row
from osri_enc2_spec import BUDGETS, Config, VERSION, components
from osri_enc2_plan import prepare, plan
from osri_enc2_order import optimize_order
from osri_enc2_dither import quantize
from osri_enc2_metrics import metrics
from osri_enc2_pack import pack
from osri_enc2_decode import decode


def encode(source, budget, cfg=Config()):
    pixels = np.asarray(source, dtype=np.int32)
    palettes, orders, _ = plan(pixels, budget, cfg)
    return pack(palettes, orders, quantize(pixels, palettes, orders, cfg), budget)


def greedy(pixels, budget, active):
    image = Image.fromarray(pixels.astype(np.uint8))
    endpoints = np.array([components(quantized_colors(image.crop((0, y, 640, y + 1))))
                          for y in range(400)])
    history = [endpoints[0]]
    for y in range(1, 400):
        p = (optimize_row(history[-1], pixels[y:y + 2], endpoints[y:y + 2], budget, 1)
             if y in active else history[-1].copy())
        history.append(p)
    orders = [()] + [tuple(np.flatnonzero(np.any(a != b, axis=1)).tolist())
                     for a, b in zip(history, history[1:])]
    return np.array(history), orders


def compare(path, output_dir, cfg=Config(), row_start=1, row_count=399):
    path, out = Path(path), Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)
    pixels = np.asarray(resize_image(path), dtype=np.int32)
    active = set(range(row_start, row_start + row_count))
    if not active or min(active) < 1 or max(active) >= 400:
        raise ValueError('row range must be within 1..399')
    prepared = prepare(pixels, cfg)
    print(json.dumps(dict(stage='prepare', **prepared[2])), flush=True)
    rows, pictures = [], []
    scope = f'rows={row_start}:{row_start + row_count};full={row_count == 399}'
    for budget in BUDGETS:
        start = time.perf_counter()
        palettes, orders, stats = plan(pixels, budget, cfg, prepared, active)
        enc_seconds = time.perf_counter() - start
        print(json.dumps(dict(stage='plan', B=budget, **stats)), flush=True)
        start = time.perf_counter()
        old_p, ascending = greedy(pixels, budget, active)
        greedy_seconds = time.perf_counter() - start
        start = time.perf_counter()
        improved = [()] + [optimize_order(pixels[y], old_p[y - 1], old_p[y], cfg)[1]
                           for y in range(1, 400)]
        ordered_seconds = greedy_seconds + time.perf_counter() - start
        for method, p, order, seconds in (('enc2', palettes, orders, enc_seconds),
                                         ('greedy-index', old_p, ascending, greedy_seconds),
                                         ('greedy-order', old_p, improved, ordered_seconds)):
            for dither in (False, True):
                for timed in (False, True):
                    start = time.perf_counter()
                    ix = quantize(pixels, p, order, cfg, timed=timed, dither=dither)
                    quantize_seconds = time.perf_counter() - start
                    start = time.perf_counter()
                    score, visible = metrics(pixels, p, order, ix, cfg, timed=timed)
                    metrics_seconds = time.perf_counter() - start
                    start = time.perf_counter()
                    crc = ''
                    if method == 'enc2' and dither and timed:
                        data = pack(p, order, ix, budget)
                        dp, do, di, _ = decode(data, budget)
                        assert np.array_equal(dp, p) and do == order and np.array_equal(di, ix)
                        (out / f'enc2-b{budget}.osri').write_bytes(data)
                        crc = f'{struct.unpack_from("<I", data, 48)[0]:08x}'
                        pictures.append(visible.astype(np.uint8))
                    pack_seconds = time.perf_counter() - start
                    rows.append(dict(image=str(path.resolve()), B=budget, method=method,
                                     scope=scope, dither=dither, timed=timed, **score,
                                     plan_seconds=seconds, quantize_seconds=quantize_seconds,
                                     metrics_seconds=metrics_seconds, pack_seconds=pack_seconds,
                                     seconds=seconds + quantize_seconds + metrics_seconds + pack_seconds,
                                     RSS_KiB=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                                     CRC=crc))
        # Write incremental tables too; a later slow budget does not erase evidence.
        write_tables(out, rows, path, cfg, prepared[2])
    Image.fromarray(np.concatenate(pictures, axis=0)).save(out / 'timing-model.png')
    return rows


def write_tables(out, rows, source, cfg, stages):
    with (out / 'comparison.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        for row in rows:
            writer.writerow({k: json.dumps(v) if isinstance(v, list) else v for k, v in row.items()})
    metadata = dict(source=str(source.resolve()), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                    config_version=VERSION, settings=asdict(cfg), stages=stages,
                    resize='img2osri.resize_image: black composite, LANCZOS thumbnail, centered 640x400',
                    RGB='RGB444 expanded by 17; original RGB8 optimization input',
                    diffusion='one signed integer FS pass, LTR, per-contribution truncation toward zero',
                    numpy=np.__version__, Pillow=pillow_version)
    text = '# enc2 S1 comparison\n\n' + '```json\n' + json.dumps(metadata, indent=2) + '\n```\n\n'
    text += ('Scope: ' + rows[0]['scope'] +
             '. Only listed rows and the initial palette are optimized; other palettes are frozen after forward initialization.\n\n')
    text += '|B|method|FS|timed|SSE|PSNR|blur MSE|OUT max/mean|K total/max|width|seconds|CRC|\n'
    text += '|---|---|---|---|---|---|---|---|---|---|---|---|\n'
    for r in rows:
        text += (f'|{r["B"]}|{r["method"]}|{r["dither"]}|{r["timed"]}|{r["SSE"]}|{r["PSNR"]:.6f}|'
                 f'{r["blur_MSE"]:.6f}|{r["OUT_max"]}/{r["OUT_mean"]:.6f}|{r["K_total"]}/{r["K_max"]}|'
                 f'{r["spill_width"]}|{r["seconds"]:.3f}|{r["CRC"]}|\n')
    text += '\nPer-boundary OUT, changed slots and K_y (all 399, including zeros) are in the CSV.\n'
    (out / 'comparison.md').write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input')
    parser.add_argument('--output-dir', required=True)
    parser.add_argument('--row-start', type=int, default=1)
    parser.add_argument('--row-count', type=int, default=399)
    args = parser.parse_args()
    compare(args.input, args.output_dir, row_start=args.row_start, row_count=args.row_count)


if __name__ == '__main__':
    main()
