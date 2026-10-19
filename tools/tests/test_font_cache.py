"""T7b-b3 S1: glyph cache host tests (glyph cache design ⑥, b4⑥) and mutations.

Runs userland/lib/font/tests/cache.rs (small slot layouts: LRU by age, hash
chain unlink, class independence, invalid-slot preference, age ties/wrap,
setting generation, fit limits, reuse). --mutate rebuilds a copy of the crate
per mutation and requires the named test to fail at run time."""
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
    ('lookup-ignores-set', 'src/cache.rs', 'if h.valid != 0 && h.key() == key {',
     'if h.valid != 0 && h.codepoint == key.cp && h.px == key.px {', 'setting_change_old_entries_never_hit'),
    ('age-not-refreshed', 'src/cache.rs', '        h.age = self.age;\n        self.set_hdr(slot, &h)?;',
     '        self.set_hdr(slot, &h)?;', 'lru_a_b_a_c_evicts_b'),
    ('unlink-head-only', 'src/cache.rs', '        if cur == target {\n            self.set_head(bucket, h.next);',
     '        if true {\n            self.set_head(bucket, h.next);', 'collision_unlink_middle_head_tail'),
    ('no-invalid-preference', 'src/cache.rs', '            if h.valid == 0 {\n                victim = slot;',
     '            if false && h.valid == 0 {\n                victim = slot;', 'invalid_slot_preferred'),
    ('age-tie-highest-slot', 'src/cache.rs', 'if best.is_none_or(|age| h.age < age) {',
     'if best.is_none_or(|age| h.age <= age) {', 'age_tie_lowest_slot'),
    ('age-wrap-kept', 'src/cache.rs', '        if self.age == u32::MAX {\n            self.stats.age_wrap',
     '        if false {\n            self.stats.age_wrap', 'age_wrap_invalidates_all'),
    ('slot-width-unchecked', 'src/cache.rs', '(usize::from(e.width) > class.width\n',
     '(false\n', 'fit_limits_per_glyph'),
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
