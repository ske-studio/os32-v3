"""T7b-b3 S1 / TASK_FONTPERF F2: glyph cache host tests (glyph cache design ⑥, b4⑥,
fontperf design §3 and §7 ①) and mutations.

Runs userland/lib/font/tests/cache.rs (64/128KiB capacity, cell fit boundaries
and cell copy, clock second chance / G-1 / G-2 / probe bound when full, hash
chain unlink, class independence, setting generation, fit limits, reuse).
--mutate rebuilds a copy of the crate per mutation and requires the named test
to fail at run time."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CRATE = ROOT / 'userland/lib/font'
sys.path.insert(0, str(Path(__file__).resolve().parent))

MUTATIONS = [
    # b4⑥: an entry of another set generation must never hit.
    ('lookup-ignores-set', 'src/cache.rs', 'if m.valid && m.key == key {',
     'if m.valid && m.key.cp == key.cp && m.key.px == key.px {', 'setting_change_old_entries_never_hit'),
    ('unlink-head-only', 'src/cache.rs', '        if cur == target {\n            self.set_head(bucket, m.next);',
     '        if true {\n            self.set_head(bucket, m.next);', 'collision_unlink_middle_head_tail'),
    ('slot-width-unchecked', 'src/cache.rs', '&& (usize::from(e.width) > width\n',
     '&& (false\n', 'fit_limits_per_glyph'),
    # F2 (fontperf design §7 ①): cells, clock, G-1.
    ('fit-always-big', 'src/cache.rs', '16 if self.n_cell > 0 && fits_cell(e) =>',
     '16 if false && fits_cell(e) =>', 'cell_fit_boundaries'),
    # F-15 (c): rows outside the cell are clipped, not refused.
    ('clip-missing', 'src/cache.rs', '        if !(0..CELL_H_PX as i32).contains(&y) {\n            continue;',
     '        if !(0..CELL_H_PX as i32).contains(&y) {\n            return None;', 'cell_clip_matches_unclipped_drawing'),
    ('cell-shift-dropped', 'src/cache.rs', '        } else if left >= 0 {\n            v >> left',
     '        } else if left >= 0 {\n            v', 'cell_copy_matches_placement'),
    ('clock-ignores-ref', 'src/cache.rs', 'if !m.valid || !m.referenced {',
     'if !m.valid || !m.referenced || slot >= first {', 'clock_second_chance'),
    ('hand-kept-on-invalidate', 'src/cache.rs', '        self.hand = [0; CLASSES];\n        self.stats.invalidate',
     '        self.stats.invalidate', 'invalidate_resets_fill_and_hand'),
]


def build_and_run(crate, work, test, label, env=None):
    env = {**os.environ, **(env or {}), 'CARGO_TARGET_DIR': str(work / 'target')}
    build = subprocess.run(['cargo', 'test', '--release', '--offline', '--manifest-path', str(crate / 'Cargo.toml'),
                            '--test', test, '--no-run', '--message-format=json'],
                           env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    messages = [json.loads(line) for line in build.stdout.splitlines() if line.startswith('{')]
    diagnostics = build.stderr + ''.join(m.get('message', {}).get('rendered', '') for m in messages)
    assert build.returncode == 0, diagnostics  # a compile failure never counts as RED
    exe = next(m['executable'] for m in messages if m.get('executable'))
    result = subprocess.run([exe, '--nocapture', '--test-threads=1'], env=env,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (work / (label + '.log')).write_text(diagnostics + result.stdout + result.stderr)
    return result


def run(test, mutations, mutate, env=None):
    from test_font_reader import product_grep
    product_grep()
    with tempfile.TemporaryDirectory(prefix='font-' + test + '-') as directory:
        work = Path(directory)
        normal = build_and_run(CRATE, work, test, 'normal', env)
        print(normal.stdout, end='', flush=True)
        assert normal.returncode == 0, normal.stdout + normal.stderr
        # The normal run above is the control for every mutation.
        if mutate:
            copy = work / 'userland/lib/font'
            shutil.copytree(CRATE, copy, ignore=shutil.ignore_patterns('target'))
            fonts = work / 'assets/fonts'
            fonts.parent.mkdir(parents=True)
            fonts.symlink_to(ROOT / 'assets/fonts', target_is_directory=True)
            for label, path, old, new, expected in mutations:
                original = (CRATE / path).read_text()
                assert original.count(old) == 1, label
                (copy / path).write_text(original.replace(old, new))
                result = build_and_run(copy, work, test, label, env)
                (copy / path).write_text(original)
                # The failures list names it (a test that prints puts FAILED on a later line).
                assert result.returncode == 101 and ('\n    ' + expected + '\n') in result.stdout, \
                    (label, result.stdout, result.stderr)
                print('RED runtime ' + label + ' (compiled): ' + expected, flush=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mutate', action='store_true')
    return run('cache', MUTATIONS, ap.parse_args().mutate)


if __name__ == '__main__':
    raise SystemExit(main())
