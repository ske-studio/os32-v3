"""T7b-a4: streamed glyphs vs whole-file outlines, faults, bounded scratch.

JF fixture uses the a1 Shinonome investigation's pinned original. Prefer the
existing investigation copy/archive; otherwise obtain the same archive for
host testing only. Nothing is installed into the product's font assets.
"""
from pathlib import Path
import argparse
import hashlib
import sys
import json
import os
import shutil
import subprocess
import tempfile

from test_font_reader import product_grep

ROOT = Path(__file__).resolve().parents[2]
CRATE = ROOT / 'userland/lib/font'
JF_HASH = '5e265e45349b3328afa67dc3905a3ca3c628cf7c7e0eccea9c7ce8a8acc127cc'
MUTATIONS = [
    ('close-range', '                self.first_edge = self.edges;\n                self.current = None;',
     '                self.current = None;', 'empty_contour_keeps_its_own_edge_range'),
    ('cycle', '.contains(&gid)',
     '.contains(&gid) && false', 'cycles_and_runs'),
    ('run', 'if run > left {\n            return Err(Error::Invalid);\n        }',
     'if run > left { return Ok(()); }', 'cycles_and_runs'),
    ('points', 'if usage.points > MAX_POINTS', 'if usage.points >= MAX_POINTS', 'simple_and_composite_limits'),
    ('contours', 'if usage.contours > MAX_CONTOURS', 'if usage.contours >= MAX_CONTOURS', 'simple_and_composite_limits'),
    ('components', 'if local > MAX_COMPONENTS', 'if local >= MAX_COMPONENTS', 'simple_and_composite_limits'),
    ('depth', 'if depth > MAX_COMPONENT_DEPTH', 'if depth >= MAX_COMPONENT_DEPTH', 'simple_and_composite_limits'),
    ('span', 'len > MAX_GLYPH_SPAN_BYTES', 'len >= MAX_GLYPH_SPAN_BYTES', 'byte_limits'),
    ('total-bytes', 'len > dst.len()', 'len >= dst.len()', 'byte_limits'),
    ('edges', '.get_mut(self.edges)', '.get_mut(self.edges + 1)', 'edge_limit_independent'),
    ('outline-points', '.get_mut(index)', '.get_mut(index + 1)', 'implicit_point_capacity'),
    ('short-read', 'if source.read_at(offset, dst)? != dst.len()',
     'if source.read_at(offset, dst)? != dst.len() && false', 'every_mid_read_failure_has_zero_commit'),
    ('partial-commit', 'self.committed = (0, 0, 0);', 'self.committed = (1, 1, 1);', 'every_mid_read_failure_has_zero_commit'),
]


def shinonome(work):
    path = os.environ.get('OS32_FONT_SHINONOME')
    if path is None:
        # The single fetch path (pinned zip/TTF SHA-256); --zip for offline runs.
        cmd = [sys.executable, '-B', str(ROOT / 'tools/fetch_fonts.py'),
               '--family', 'jfdot-shinonome16', '--font-dir', str(work / 'jf')]
        archive = Path.home() / 'os32-tmp/run/shinonome/sources/jfdotfont-20150527.zip'
        if archive.exists():
            cmd += ['--zip', str(archive)]
        subprocess.run(cmd, check=True)
        path = work / 'jf/JF-Dot-Shinonome16.ttf'
    path = Path(path)
    assert hashlib.sha256(path.read_bytes()).hexdigest() == JF_HASH, 'JF original hash'
    print('JF fixture SHA256=' + JF_HASH, flush=True)
    return path


def build_run(crate, work, env, label, test=None):
    build = subprocess.run(['cargo', 'test', '--release', '--offline', '--manifest-path', str(crate / 'Cargo.toml'),
                            '--test', 'outline', '--no-run', '--message-format=json'],
                           env={**env, 'CARGO_TARGET_DIR': str(work / 'target')},
                           text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    messages = [json.loads(line) for line in build.stdout.splitlines() if line.startswith('{')]
    diagnostics = build.stderr + ''.join(m.get('message', {}).get('rendered', '') for m in messages)
    assert build.returncode == 0, diagnostics  # compile failures are never RED
    exe = next(m['executable'] for m in messages if m.get('executable'))
    result = subprocess.run([exe, *([test, '--exact'] if test else []), '--nocapture', '--test-threads=1'],
                            env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (work / (label + '.log')).write_text(diagnostics + result.stdout + result.stderr)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args()
    for family in ('ipaex', 'inconsolata'):
        subprocess.run(['python3', str(ROOT / 'tools/fetch_fonts.py'), '--family', family], check=True)
    product_grep()
    with tempfile.TemporaryDirectory(prefix='font-outline-') as directory:
        work = Path(directory)
        jf = shinonome(work)
        cps = []
        for a in range(0x21, 0x7f):
            for b in range(0x21, 0x7f):
                try:
                    cps.append(ord((b'\x1b$B' + bytes([a, b]) + b'\x1b(B').decode('iso2022_jp')))
                except UnicodeDecodeError:
                    pass
        assert len(cps) == 6879
        jis = work / 'jis.txt'
        jis.write_text(''.join(str(cp) + '\n' for cp in cps))
        env = {**os.environ, 'OS32_FONT_SHINONOME': str(jf), 'OS32_FONT_JIS': str(jis)}
        normal = build_run(CRATE, work, env, 'normal')
        print(normal.stdout, end='', flush=True)
        assert normal.returncode == 0, normal.stderr
        copy = work / 'userland/lib/font'
        shutil.copytree(CRATE, copy, ignore=shutil.ignore_patterns('target'))
        assets = work / 'assets'
        assets.mkdir()
        (assets / 'fonts').symlink_to(ROOT / 'assets/fonts', target_is_directory=True)
        limits = copy / 'src/limits.rs'
        original_limits = limits.read_text()
        limits.write_text(original_limits.replace('MAX_EDGES: usize = 512;', 'MAX_EDGES: usize = 128;'))
        edges = build_run(copy, work, env, 'edges-128', 'edge_limit_independent')
        print(edges.stdout, end='', flush=True)
        assert edges.returncode == 0, edges.stderr
        point_limit = 'MAX_OUTLINE_POINTS: usize = 2 * MAX_POINTS + MAX_CONTOURS;'
        for delta in (-1, 0, 1):
            limits.write_text(original_limits.replace(point_limit, point_limit[:-1] + f' {delta:+d};'))
            points = build_run(copy, work, env, 'outline-points-' + str(delta), 'implicit_point_capacity')
            print(points.stdout, end='', flush=True)
            assert points.returncode == 0, points.stderr
        limits.write_text(original_limits)
        if args.mutate:
            originals = {name: (copy / 'src' / name).read_text() for name in ('outline.rs', 'reader.rs')}
            for label, old, new, expected in MUTATIONS:
                for name, original in originals.items():
                    (copy / 'src' / name).write_text(original)
                name = 'reader.rs' if label in ('span', 'total-bytes', 'short-read') else 'outline.rs'
                assert originals[name].count(old) == 1, label
                (copy / 'src' / name).write_text(originals[name].replace(old, new))
                changed_limits = original_limits
                if label == 'edges':
                    changed_limits = original_limits.replace('MAX_EDGES: usize = 512;', 'MAX_EDGES: usize = 128;')
                limits.write_text(changed_limits)
                result = build_run(copy, work, env, label, expected)
                assert result.returncode == 101 and ('test ' + expected + ' ... FAILED') in result.stdout, (label, result.stdout, result.stderr)
                # Distinguish assertion failure from arithmetic panic/hang.
                diagnostic = 'exact implicit-point capacity rejected' if label == 'outline-points' else 'assertion'
                assert diagnostic in result.stderr, (label, result.stderr)
                print('RED runtime ' + label + ' (compiled): ' + expected, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
