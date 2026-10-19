"""T7b-a3: real-font Rust differential tests and runtime reader mutations.

T7b-a7: the face opens in place and keeps the cmap search arrays resident when
they fit MAX_CMAP_RESIDENT_BYTES; both cmap paths are compared on every real
font (Shinonome included) and on the residency boundary fixtures."""
from pathlib import Path
import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CRATE = ROOT / 'userland/lib/font'
LRU_PROMOTION = """            let rank = self.slots.get(index).ok_or(Error::Invalid)?.lru;
            for slot in &mut self.slots {
                if slot.valid != 0 && slot.lru < rank {
                    slot.lru += 1;
                }
            }
            self.slots.get_mut(index).ok_or(Error::Invalid)?.lru = 0;"""
MUTATIONS = [
    ('LRU-to-FIFO', LRU_PROMOTION,
     '            if cached.is_none() {\n' + LRU_PROMOTION + '\n            }',
     'read_cache_lru'),
    ('offset-length-overflow', 'let end = t.offset.checked_add(t.length).ok_or(Error::Bounds)?;',
     'let end = t.offset.wrapping_add(t.length);', 'offset_length_overflow'),
    ('directory-order', 'if t.tag <= previous {',
     'if false && t.tag <= previous {', 'reversed_order'),
    ('idRangeOffset-minus-one', 'if range % 2 != 0 || first < 16 + count * 8',
     'if false && (range % 2 != 0 || first < 16 + count * 8)', 'id_range_offset_boundaries'),
    # T7b-a7 resident cmap.
    ('cmap-never-resident', 'if !allowed || len as usize > MAX_CMAP_RESIDENT_BYTES {',
     'if true || len as usize > MAX_CMAP_RESIDENT_BYTES {', 'cmap_resident_limit_boundary'),
    ('cmap-resident-limit', 'if !allowed || len as usize > MAX_CMAP_RESIDENT_BYTES {',
     'if !allowed || len as usize > MAX_CMAP_RESIDENT_BYTES - 8 {', 'cmap_resident_limit_boundary'),
    ('resident-past-arrays', '        if end > self.len {\n            return None;',
     '        if end > self.len + 2 {\n            return None;', 'id_range_offset_boundaries'),
    ('streaming-ignored', 'if !allowed || len as usize', 'if len as usize', 'open_in_place_reopen_and_close'),
]


def product_grep():
    for path in sorted((CRATE / 'src').glob('*.rs')):
        source = re.sub(r'//[^\n]*', '', path.read_text())
        assert not re.search(r'\b(?:unwrap|expect|panic|unreachable|unimplemented|todo|get_unchecked|unsafe)\b|\b(?:Vec|Box|alloc)\b', source), path
        # Every subscript in the production reader is a type/array declaration;
        # Lifetime-qualified slices are declarations too; data access must use
        # get/get_mut, iterators, or checked conversions.
        assert not re.search(r"(?<!')\b(?!mut\b)\w+\s*\[[^\]\n]+\]", source), path
    print('PASS product grep: no unwrap/expect/panic/unchecked subscript/heap allocation', flush=True)


def build_and_run(crate, work, label):
    env = {**os.environ, 'CARGO_TARGET_DIR': str(work / 'target')}
    build = subprocess.run(['cargo', 'test', '--release', '--offline', '--manifest-path', str(crate / 'Cargo.toml'),
                            '--test', 'reader', '--no-run', '--message-format=json'],
                           env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert build.returncode == 0, build.stderr + "".join(json.loads(line).get("message", {}).get("rendered", "") for line in build.stdout.splitlines() if line.startswith("{"))  # compile failure never counts as RED
    artifacts = [json.loads(line) for line in build.stdout.splitlines() if line.startswith('{')]
    exe = next(a['executable'] for a in artifacts if a.get('executable'))
    result = subprocess.run([exe, '--nocapture', '--test-threads=1'], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (work / (label + '.log')).write_text(build.stderr + result.stdout + result.stderr)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args()
    # Existing consent/cache path; no missing-font SKIP.
    for family in ('ipaex', 'inconsolata'):
        subprocess.run(['python3', str(ROOT / 'tools/fetch_fonts.py'), '--family', family], check=True)
    product_grep()
    # Imported here: test_font_outline imports this module (product_grep).
    from test_font_outline import shinonome
    with tempfile.TemporaryDirectory(prefix='font-reader-') as directory:
        work = Path(directory)
        os.environ['OS32_FONT_SHINONOME'] = str(shinonome(work))
        normal = build_and_run(CRATE, work, 'normal')
        print(normal.stdout, end='', flush=True)
        assert normal.returncode == 0, normal.stderr
        copy = work / 'userland/lib/font'
        shutil.copytree(CRATE, copy, ignore=shutil.ignore_patterns('target'))
        fonts = work / 'assets/fonts'
        fonts.parent.mkdir(parents=True)
        fonts.symlink_to(ROOT / 'assets/fonts', target_is_directory=True)
        limits = copy / 'src/limits.rs'
        original_limits = limits.read_text()
        old = 'pub const READ_CACHE_SLOTS: usize = 16;'
        assert original_limits.count(old) == 1
        limits.write_text(original_limits.replace(old, 'pub const READ_CACHE_SLOTS: usize = 4;'))
        other = build_and_run(copy, work, 'slots-4')
        print(other.stdout, end='', flush=True)
        assert other.returncode == 0, other.stderr
        limits.write_text(original_limits)
        if args.mutate:
            original = (CRATE / 'src/reader.rs').read_text()
            for label, old, new, expected in MUTATIONS:
                assert original.count(old) == 1, label
                (copy / 'src/reader.rs').write_text(original.replace(old, new))
                result = build_and_run(copy, work, label)
                assert result.returncode == 101 and ('test ' + expected + ' ... FAILED') in result.stdout, (label, result.stdout, result.stderr)
                print(result.stdout, end='', flush=True)
                print('RED runtime ' + label + ' (compiled): ' + expected, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
