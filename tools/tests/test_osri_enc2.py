"""S1 integer oracle, timing/prefix, two-sided planning and v2 round trips."""
CHECK = dict(name='osri-enc2', tier=1, needs=['make:osri-enc2-core'], budget=90,
             covers=['gfx/osri_profile.h', 'tools/osri_profile.py', 'tools/osri_enc2_spec.py', 'tools/osri_enc2_master.py',
                     'tools/osri_enc2_tree.py', 'tools/osri_enc2_row.py',
                     'tools/osri_enc2_plan.py', 'tools/osri_enc2_order.py',
                     'tools/osri_enc2_native.py',
                     'tools/osri_enc2_timing.py', 'tools/osri_enc2_dither.py',
                     'tools/osri_enc2_pack.py', 'tools/osri_enc2_decode.py',
                     'tools/osri_enc2_metrics.py', 'tools/osri_enc2_compare.py',
                     'tools/img2osri.py'])

import argparse
from dataclasses import replace
import hashlib
import itertools
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, os.environ.get('ENC2_TOOLS', str(ROOT / 'tools')))
from osri_enc2_spec import (BUDGETS, Config, allowed, centroid, codes, components,
                            distance, out_count, partials, window)
from osri_enc2_master import master
from osri_enc2_tree import build_tree, row_candidates
from osri_enc2_timing import cuts, display_palette, render_row
from osri_enc2_order import OrderCost, optimize_order
from osri_enc2_row import RowSearch
from osri_enc2_plan import plan, objective, prepare
from osri_enc2_dither import quantize, trunc16
from osri_enc2_pack import pack
from osri_enc2_decode import decode
from osri_enc2_metrics import metrics, blur_numerator
from osri_enc2_compare import write_tables


def rejects(function):
    try:
        function()
    except ValueError:
        return
    assert False, 'expected ValueError'


def scalar_out(a, b):
    changes = [[int(a[i, c]) != int(b[i, c]) for c in range(3)] for i in range(16)]
    return sum(sum(row) for row in changes) + max(0, sum(any(row) for row in changes) - 1)


def scalar_visible(old, new, order, width, cfg):
    result = np.tile(old[None, :, :] * 17, (width, 1, 1))
    j = 0
    for pos, slot in enumerate(order):
        j += bool(pos)
        for c in (1, 0, 2):
            if old[slot, c] != new[slot, c]:
                j += 1
                for x in range(width):
                    if ((cfg.start_delay_ns + j * 650 - 5900 - cfg.pixel0_offset_ns) * width
                            <= x * 25900):
                        result[x, slot, c] = int(new[slot, c]) * 17
    return result


def scalar_quantize(pixels, palettes, orders, cfg, timed=True, vertical=True):
    h, w = pixels.shape[:2]
    err = [[0] * 3 for _ in range(w + 2)]
    output = np.zeros((h, w), dtype=np.uint8)
    for y in range(h):
        table = (scalar_visible(palettes[y - 1], palettes[y], orders[y], w, cfg) if y and timed
                 else np.tile(palettes[y][None, :, :] * 17, (w, 1, 1)))
        nxt = [[0] * 3 for _ in range(w + 2)]
        for x in range(w):
            value = [max(0, min(255, int(pixels[y, x, c]) + err[x + 1][c])) for c in range(3)]
            index = min(range(16), key=lambda i: sum((value[c] - int(table[x, i, c])) ** 2 for c in range(3)))
            output[y, x] = index
            for c in range(3):
                e = value[c] - int(table[x, index, c])
                for target, pos, weight in ((err, x + 2, 7), (nxt, x, 3), (nxt, x + 1, 5), (nxt, x + 2, 1)):
                    target[pos][c] += (abs(e * weight) // 16) * (1 if e >= 0 else -1)
        err = nxt if vertical else [[0] * 3 for _ in range(w + 2)]
    return output


def check_integer_master_tree():
    points = np.array([[0, 1, 255], [7, 128, 17], [255, 254, 13]], dtype=np.int32)
    colors = components([0, 0xfff, 0x080])
    expected = [[sum((int(a[c]) - int(b[c]) * 17) ** 2 for c in range(3)) for b in colors] for a in points]
    assert np.array_equal(distance(points, colors), expected)
    for n, sums in ((2, [17, 51, 493]), (7, [834, 19, 1785]), (3, [0, 0, 0])):
        best = min(range(4096), key=lambda q: (n * sum(v * v for v in components(q) * 17) -
                                              2 * sum(int(v) * int(s) for v, s in zip(components(q) * 17, sums)), q))
        assert int(codes(centroid(n, sums))) == best
    cfg = Config(master_iters=3)
    for pixels in (np.zeros((4, 8, 3), dtype=np.uint8),
                   np.array([[[0, 0, 0], [255, 255, 255], [128, 17, 83]]], dtype=np.uint8),
                   np.arange(96, dtype=np.uint8).reshape(4, 8, 3)):
        m, mass, losses = master(pixels, cfg)
        assert m.shape == (256, 3) and int(mass.sum()) == pixels.shape[0] * pixels.shape[1]
        assert all(a > b for a, b in zip(losses, losses[1:]))
        assert np.all(codes(m)[:-1] <= codes(m)[1:])
        other = master(pixels, cfg)
        assert np.array_equal(m, other[0]) and np.array_equal(mass, other[1])
        roots, layers = build_tree(m, mass)
        assert [len(layer) for layer in layers] == [256, 128, 64, 32, 16]
        for children, parents in zip(layers, layers[1:]):
            ids = [id(child) for parent in parents for child in parent.children]
            assert len(set(ids)) == len(children) and set(ids) == {id(n) for n in children}
        for y in range(len(pixels)):
            points, weights = window(pixels, y, cfg)
            assert len(points) == pixels.shape[1] * min(4, len(pixels) - y)
            assert list(weights[::pixels.shape[1]]) == [8, 4, 2, 1][:len(pixels) - y]
            assert 1 <= len(row_candidates(pixels, y, roots, cfg)) <= 16
    # 32 vertical stripes, 512 colors; partial G proposals need not belong to M.
    stripes = np.array([[(x // 2 % 16) * 17, y * 17, (x % 2) * 255]
                        for y in range(16) for x in range(32)], dtype=np.uint8).reshape(16, 32, 3)
    assert len(np.unique(stripes.reshape(-1, 3), axis=0)) == 512
    m, mass, _ = master(stripes, Config(master_iters=1))
    roots, _ = build_tree(m, mass)
    candidates = row_candidates(stripes, 3, roots)
    p = np.array([15, 0, 15])
    proposals = partials(p, candidates)
    assert any(q[0] == 15 and q[2] == 15 and q[1] != 0 for q in proposals)
    assert (1, 8, 3) in partials(np.array([1, 2, 3]), np.array([[9, 8, 7]]))
    assert int(codes(centroid(2, [34, 272, 102]))) == 0x183


def check_out_timing_order():
    a = np.zeros((16, 3), dtype=np.int32)
    b = a.copy(); b[:12, 1] = 15
    assert out_count(a, a) == 0 and out_count(a, b) == scalar_out(a, b) == 23
    b[:12] = 15
    assert out_count(a, b) == scalar_out(a, b) == 47 and allowed(a, b, 47)
    b[12, 1] = 1
    assert not allowed(a, b, 47)
    b[11, 2] = 0
    assert scalar_out(a, b) == 48 and not allowed(a, b, 47)
    b = a.copy(); b[:2, 1] = 15
    boundary, writes = cuts(a, b, (1, 0), 640)
    assert [(s, c, t) for _, s, c, t in writes] == [(1, 1, 1225), (0, -1, 1875), (0, 1, 2525)]
    # The older IN_NS start-delay example remains a configurable calibration fixture.
    _, writes = cuts(a, b, (1, 0), 640, Config(start_delay_ns=1150))
    assert [t for _, _, _, t in writes] == [1800, 2450, 3100]
    b[:4] = 15
    for offset in (-100, 0, 100):
        cfg = Config(start_delay_ns=5250, pixel0_offset_ns=offset)
        visible = display_palette(a, b, (3, 2, 1, 0), 640, cfg)
        assert np.array_equal(visible, scalar_visible(a, b, (3, 2, 1, 0), 640, cfg))
    # First G finishes exactly at x=0: G new, R/B old; just after stays old at x=0.
    assert list(display_palette(a, b, (0, 1, 2, 3), 640, Config(start_delay_ns=5250))[0, 0]) == [0, 255, 0]
    assert list(display_palette(a, b, (0, 1, 2, 3), 640, Config(start_delay_ns=5251))[0, 0]) == [0, 0, 0]
    points = np.array([[[i * 37 % 256, i * 71 % 256, i * 19 % 256] for i in range(24)]])[0]
    cfg = Config(start_delay_ns=6000)
    indices = np.arange(24) % 4
    cost = OrderCost(points, a, b, indices)
    for order in itertools.permutations(range(4)):
        rgb = scalar_visible(a, b, order, 24, cfg)[np.arange(24), indices]
        assert cost.score(order, cfg) == int(np.sum((points.astype(np.int64) - rgb) ** 2))
    permutations = list(itertools.permutations(range(4)))
    assert list(cost.score_many(permutations, cfg)) == [cost.score(o, cfg) for o in permutations]
    sse, order = optimize_order(points, a, b, cfg)
    nearest = distance(points, b).argmin(axis=1)
    oracle = OrderCost(points, a, b, nearest)
    assert sse == oracle.score(order, cfg)
    assert sse <= oracle.score((0, 1, 2, 3), cfg)
    retained = tuple(reversed(order))
    assert optimize_order(points, a, b, cfg, retained)[0] <= oracle.score(retained, cfg)
    score, adapted = optimize_order(points, a, b, cfg, (3, 15))
    assert sorted(adapted) == [0, 1, 2, 3] and score == oracle.score(adapted, cfg)
    # Equal OUT count, different usage creates a visible order-dependent error.
    p = np.zeros((2, 640, 3), dtype=np.int32)
    p[1] = [255, 0, 0]
    b = a.copy(); b[:12] = [15, 0, 0]
    ix = np.zeros((2, 640), dtype=np.uint8)
    forward = metrics(p, np.array([a, b]), [(), tuple(range(12))], ix)[0]
    reverse = metrics(p, np.array([a, b]), [(), tuple(reversed(range(12)))], ix)[0]
    assert forward['K_total'] < reverse['K_total'] and forward['SSE'] < reverse['SSE']
    unused = ix.copy(); unused[1] = 15
    assert metrics(p, np.array([a, b]), [(), tuple(range(12))], unused)[0]['K_total'] == 0
    old = np.full((16, 3), 15, dtype=np.int32); old[0] = 0
    new = np.full((16, 3), 15, dtype=np.int32)
    points = np.zeros((16, 3), dtype=np.int32)
    cost = OrderCost(points, old, new, np.zeros(16, dtype=np.int32))
    assert cost.score((0,), Config(start_delay_ns=6000)) - cost.base < 0


def check_rows():
    pixels = np.array([[[x * 43 % 256, y * 83 % 256, (x + y) * 29 % 256]
                        for x in range(4)] for y in range(4)], dtype=np.int32)
    cfg = Config(master_iters=1, row_iters=1, row_moves=1, round_trips=1, order_passes=1)
    prepared = prepare(pixels, cfg)
    for budget in BUDGETS:
        p, orders, stats = plan(pixels, budget, cfg, prepared)
        again, same_orders, _ = plan(pixels, budget, cfg, prepared)
        assert np.array_equal(p, again) and orders == same_orders
        assert all(scalar_out(a, b) <= budget for a, b in zip(p, p[1:]))
        assert all(a >= b for a, b in zip(stats['j_trace'], stats['j_trace'][1:]))
        assert all(a > b for _, a, b in stats['visits'])
        assert objective(pixels, p, orders, cfg) == stats['j_trace'][-1]
    # Direct coordinate Lloyd keeps every unselected physical slot fixed.
    p = np.zeros((16, 3), dtype=np.int32); p[0] = [2, 2, 2]; p[1] = [12, 4, 12]
    x = np.full((2, 4, 3), [33, 150, 33], dtype=np.int32)
    x[:, 2:] = [200, 90, 200]
    old = p.copy(); old[0, 1] = 0
    search = RowSearch(x, 1, old, None, (), 15, np.array([[2, 9, 2]]),
                       np.array([[2, 9, 2]]), cfg)
    value = search.lloyd(p, [0])
    assert np.array_equal(value[1:], p[1:]) and value[0, 1] != p[0, 1]
    p = np.full((16, 3), 15, dtype=np.int32); p[0] = [1, 2, 3]
    old = p.copy(); old[0, 1] = 0
    x = np.full((2, 4, 3), [17, 136, 51], dtype=np.int32)
    m = np.array([[9, 8, 7]])
    search = RowSearch(x, 1, old, None, (), 1, m, m, cfg)
    value = search.lloyd(p, [0])
    assert tuple(value[0]) == (1, 8, 3) and not any(np.array_equal(value[0], q) for q in m)
    assert np.array_equal(value[1:], p[1:])
    # y+1 timing delta is part of every saved candidate's score, including negative delta.
    p = np.zeros((16, 3), dtype=np.int32)
    following = p.copy(); following[:12] = 15
    x = np.zeros((3, 16, 3), dtype=np.int32); x[2] = 255
    cfg = Config(start_delay_ns=6000, row_iters=0, row_moves=0)
    order = tuple(reversed(range(12)))
    search = RowSearch(x, 1, p, following, order, 47, following, following[:1], cfg)
    value = search.evaluate(p)
    pts, w = window(x, 1, cfg)
    f = int(np.dot(distance(pts, p).min(axis=1).astype(np.int64), w))
    ids = distance(x[2], following).argmin(axis=1)
    oracle = OrderCost(x[2], p, following, ids)
    expected = f + 8 * (oracle.score(value[2], cfg) - oracle.base)
    assert value[0] == expected and expected != f
    trial = p.copy(); trial[0, 1] = 1
    trial_value = search.evaluate(trial)
    base_j = objective(x, np.array([p, p, following]), [(), value[1], value[2]], cfg)
    new_j = objective(x, np.array([p, trial, following]), [(), trial_value[1], trial_value[2]], cfg)
    assert new_j - base_j == trial_value[0] - value[0]
    # Cached nearest/runner-up distances equal a fresh all-slot evaluation.
    palette = components(np.arange(16) * 0x111)
    fast = RowSearch(x, 1, palette, None, (), 47, palette, palette, cfg)
    fresh = RowSearch(x, 1, palette, None, (), 47, palette, palette, cfg)
    neighbors = fast.neighbors(palette)
    for slot in range(16):
        trial, fast_value = fast.evaluate_slot(palette, slot, (15, 0, 0), neighbors)
        assert fast_value == fresh.evaluate(trial)


def check_diffusion():
    assert trunc16(-7) == 0 and trunc16(-17) == -1
    x = np.array([[[i * 47 % 256, j * 91 % 256, (i + j) * 37 % 256]
                   for i in range(8)] for j in range(4)], dtype=np.int32)
    p = np.zeros((4, 16, 3), dtype=np.int32)
    p[:, 1] = 15
    p[1:, 2] = [15, 0, 8]
    p[2:, 3] = [2, 15, 4]
    orders = [(), (2,), (3,), ()]
    cfg = Config(start_delay_ns=12000)
    original = list(orders)
    actual = quantize(x, p, orders, cfg)
    assert np.array_equal(actual, scalar_quantize(x, p, orders, cfg))
    assert not np.array_equal(actual, scalar_quantize(x, p, orders, cfg, vertical=False))
    assert orders == original
    ideal = quantize(x, p, orders, cfg, timed=False)
    assert np.array_equal(ideal, scalar_quantize(x, p, orders, cfg, timed=False))
    assert not np.array_equal(actual, ideal)
    # First slot wins a nearest-color tie.
    duplicate = np.zeros((1, 16, 3), dtype=np.int32)
    assert not quantize(x[:1], duplicate, [()]).any()


def rehash(data):
    data[48:52] = bytes(4)
    struct.pack_into('<I', data, 48, zlib.crc32(data))
    return bytes(data)


def check_pack_metrics():
    p = np.zeros((400, 16, 3), dtype=np.int32)
    p[0] = components(list(range(16)))
    p[1:] = p[0]; p[1:, 9] = [15, 3, 1]; p[1:, 2] = [1, 15, 2]
    p[399, 15] = [7, 8, 9]
    orders = [()] + [(9, 2)] + [()] * 397 + [(15,)]
    ix = (np.arange(256000).reshape(400, 640) % 16).astype(np.uint8)
    data = pack(p, orders, ix, 47)
    assert data == pack(p, orders, ix, 47)
    checked = bytearray(data); checked[48:52] = bytes(4)
    assert struct.unpack_from('<I', data, 48)[0] == zlib.crc32(checked)
    dp, do, di, outs = decode(data)
    assert np.array_equal(dp, p) and do == orders and np.array_equal(di, ix)
    for plane in range(4):
        for x in range(16):
            assert (data[96 + plane * 32000 + x // 8] >> (7 - x % 8) & 1) == (x >> plane & 1)
    assert len(outs) == 399 and outs[0] == scalar_out(p[0], p[1]) and outs[1] == 0
    assert struct.unpack_from('<HBBHH', data, 128096)[:3] == (1, 9, 0)
    corrupt = bytearray(data); corrupt[100] ^= 1
    rejects(lambda: decode(corrupt))
    for offset, value in ((42, 1), (46, 1), (52, 1), (64, 255), (128099, 1), (128102, 1)):
        bad = bytearray(data); bad[offset] = value
        if offset == 64: bad[65] = 255
        rejects(lambda: decode(rehash(bad)))
    bad = bytearray(data); bad[128106] = 9
    rejects(lambda: decode(rehash(bad)))
    bad = bytearray(data); struct.pack_into('<H', bad, 128104, 0)
    rejects(lambda: decode(rehash(bad)))
    bad = bytearray(data); bad[128098] = 16
    rejects(lambda: decode(rehash(bad)))
    rejects(lambda: pack(p, [()] * 400, ix, 47))
    bad_ix = ix.copy(); bad_ix[0, 0] = 16
    rejects(lambda: pack(p, orders, bad_ix, 47))
    rejects(lambda: decode(data, 1))
    source = np.array([p[y, ix[y]] * 17 for y in range(400)])
    score, _ = metrics(source, p, orders, ix, timed=False)
    assert score['SSE'] == score['blur_MSE'] == 0 and score['PSNR'] == float('inf')
    tiny = np.arange(12).reshape(2, 2, 3)
    brute = np.zeros_like(tiny)
    for y in range(2):
        for x in range(2):
            for j, wy in enumerate((1, 2, 1)):
                for i, wx in enumerate((1, 2, 1)):
                    brute[y, x] += tiny[max(0, min(1, y + j - 1)), max(0, min(1, x + i - 1))] * wy * wx
    assert np.array_equal(blur_numerator(tiny), brute)
    with tempfile.TemporaryDirectory() as td:
        source_path = Path(td) / 'source'; source_path.write_bytes(b'fixture')
        row = dict(B=15, method='enc2', scope='fixture', dither=True, timed=True, **score,
                   seconds=0, RSS_KiB=1, CRC='test')
        write_tables(Path(td), [row], source_path, Config(), {})
        assert hashlib.sha256(b'fixture').hexdigest() in (Path(td) / 'comparison.md').read_text()
        assert 'actual_OUT' in (Path(td) / 'comparison.csv').read_text()


def check_cli():
    for options in (['--encoder', 'pl-b', '--outs', '15'],
                    ['--outs', '31', '--obs-outs', '31'],
                    ['--encoder', 'enc2', '--obs-outs', '47', '--hblank-outs', '5'],
                    ['--encoder', 'enc2', '--obs-outs', '15', '--obs-colors', '1'],
                    ['--encoder', 'enc2', '--obs-outs', '23', '--band', '2']):
        r = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/img2osri.py'),
                            'missing.png', '-o', 'unused.osri', *options], capture_output=True, text=True)
        assert r.returncode == 2 and 'error:' in r.stderr and 'FileNotFoundError' not in r.stderr


def check():
    check_integer_master_tree()
    check_out_timing_order()
    check_rows()
    check_diffusion()
    check_pack_metrics()
    check_cli()
    print('enc2 S1: GREEN', flush=True)


MUTATIONS = [
    ('component-cost', 'spec', 'return int(diff.sum()) + max(0, slots - 1)', 'return slots'),
    ('first-index', 'spec', 'max(0, slots - 1)', 'slots'),
    ('all-components', 'spec', 'candidate[c] if mask & (1 << c) else current[c]', 'candidate[c]'),
    ('centroid-tie', 'spec', 'np.argmin(cost, axis=1)', '15 - np.argmin(cost[:, ::-1], axis=1)'),
    ('next-delta', 'row', 'score += SPILL_WEIGHT * (actual_next - self.next_ideal)', 'score += 0'),
    ('runner-up', 'row', 'np.where(nearest == slot, second, first)', 'first'),
    ('fixed-slot', 'row', 'for slot in movable:', 'for slot in range(16):'),
    ('order', 'timing', 'for position, slot in enumerate(order):', 'for position, slot in enumerate(sorted(order)):'),
    ('pixel-boundary', 'timing', '-((-numerator) // ACTIVE_NS)', 'numerator // ACTIVE_NS'),
    ('negative-error', 'dither', 'return n // 16 if n >= 0 else -((-n) // 16)', 'return n // 16'),
    ('vertical-error', 'dither', 'error = nxt', 'error = [[0, 0, 0] for _ in range(width + 2)]'),
    ('nearest-tie', 'dither', 'int(np.sum(delta * delta, axis=1).argmin())', '15 - int(np.sum(delta * delta, axis=1)[::-1].argmin())'),
    ('crc-offset', 'pack', "struct.pack_into('<I', data, 48, zlib.crc32(data))", "struct.pack_into('<I', data, 52, zlib.crc32(data))"),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    check()
    if args.mutate:
        with tempfile.TemporaryDirectory() as td:
            tools = Path(td) / 'tools'
            tools.mkdir()
            for path in (ROOT / 'tools').glob('osri_enc2_*.py'):
                shutil.copy(path, tools / path.name)
            # The unchanged baseline import used by the comparison driver.
            shutil.copy(ROOT / 'tools/img2osri.py', tools / 'img2osri.py')
            shutil.copy(ROOT / 'tools/osri_profile.py', tools / 'osri_profile.py')
            (tools.parent / 'gfx').mkdir()
            shutil.copy(ROOT / 'gfx/osri_profile.h', tools.parent / 'gfx/osri_profile.h')
            for name, module, old, new in MUTATIONS:
                path = tools / f'osri_enc2_{module}.py'
                source = path.read_text()
                assert source.count(old) == 1, name
                path.write_text(source.replace(old, new))
                env = dict(os.environ, ENC2_TOOLS=str(tools))
                result = subprocess.run([sys.executable, '-B', __file__], env=env,
                                        capture_output=True, text=True)
                path.write_text(source)
                print(name, 'RED' if result.returncode else 'SURVIVED',
                      result.stderr.splitlines()[-1] if result.stderr else '', flush=True)
                assert result.returncode != 0 and 'AssertionError' in result.stderr, name
            # Restored control uses exactly the same copied modules and runner.
            subprocess.run([sys.executable, '-B', __file__], env=dict(os.environ, ENC2_TOOLS=str(tools)), check=True)


if __name__ == '__main__':
    main()


def run(ctx):
    argv = [sys.executable, '-B', 'tools/tests/test_osri_enc2.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)
