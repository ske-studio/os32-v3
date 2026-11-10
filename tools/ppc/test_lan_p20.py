#!/usr/bin/env python3
"""Exercise the actual boot log FIFO and runtime mutation diagnostics."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'platform/powermac/lan_log.c'
CHECK = dict(name='ppc-lan-p20', tier=1, budget=60, needs=[],
             covers=['platform/powermac/lan_log.c'],
             inputs=['tools/ppc/test_lan_p20.py', 'tools/ppc/lan_p20_test.c',
                     'platform/powermac/lan_boot.h', 'platform/powermac/kboot_log.h',
                     'arch/ppc/ppc_oea.h', 'include/types.h'])
MUTATIONS = [
    ('loss', 'add_sat(&drop_records, chunks);', '(void)chunks;', 'reached=full', 'H:A8 loss-count'),
    ('packing', 'if (s->len == LOG_BYTES) close_slot();', 'close_slot();', 'reached=fixture', 'H:packing'),
    ('oldest', 'if (n) { close_slot(); discard(n); }', 'if (n) { head = (head + 1) % LOG_SLOTS; close_slot(); discard(n); }', 'reached=full', 'H:oldest'),
    ('reentry', 'if (busy) {', 'if (0) {', 'reached=reentry', 'H:reentry'),
    ('again', 'if (result == LAN_OK && count) {', 'if ((result == LAN_OK || result == LAN_AGAIN) && count) {', 'reached=full', 'H:again'),
]


def command(args):
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, timeout=60)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        path = Path(tmp)
        def check(name, source, marker=None, diagnostic=None):
            (path / 'lan_log.c').write_text(source)
            cmd = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
                   '-nostdlib', '-static', '-fno-stack-protector', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(path), '-Iplatform/powermac', '-Iarch/ppc', '-Iinclude',
                   'tools/ppc/lan_p20_test.c', '-o', str(path / 'test')]
            result = command(cmd)
            if result.returncode:
                raise RuntimeError(f'{name} compile: {result.stderr}')
            result = command(['qemu-i386', str(path / 'test')])
            if marker is None:
                if result.returncode != 0 or not result.stdout.endswith('PASS\n'):
                    raise RuntimeError(f'{name}: {result.returncode} {result.stdout} {result.stderr}')
                print('baseline OK', flush=True)
            else:
                if result.returncode != 1 or marker + '\n' not in result.stdout or not result.stdout.endswith(diagnostic + '\n') or result.stderr:
                    raise RuntimeError(f'{name}: unexpected {result.returncode} {result.stdout} {result.stderr}')
                print(f'RED {name}: {marker}; {diagnostic}', flush=True)
        check('baseline', original)
        if args.mutate:
            for name, old, new, marker, diagnostic in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor is not unique')
                check(name, original.replace(old, new), marker, diagnostic)


if __name__ == '__main__':
    main()
