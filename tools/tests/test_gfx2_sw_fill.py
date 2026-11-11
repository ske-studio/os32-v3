"""Exercise S1 solid fill with format and sync stubs."""
import argparse
import os
from pathlib import Path
import tempfile
import host32
from mutpar import build_in_tree

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'gfx/v2/sw_fill.c'
OLD = 'x < r->x + r->w;'
NEW = 'x <= r->x + r->w;'


def run(folder, runners, mutate=False):
    exe = folder / 'sw-fill-host'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
           '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
           '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
           *['-I' + str(ROOT / d) for d in ('.', 'include', 'sdk/include/os32',
             'arch/x86', 'platform/pc98', 'mm', 'lib', 'gfx/v2')],
           str(ROOT / 'tools/tests/gfx2_sw_fill_host.c'), str(ROOT / SOURCE), '-o', str(exe)]
    edits = {}
    if mutate:
        source = (ROOT / SOURCE).read_text()
        assert source.count(OLD) == 1
        edits[SOURCE] = source.replace(OLD, NEW)
    build_in_tree(ROOT, folder, edits, [cmd])
    for runner in runners:
        result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
        assert result.returncode == (2 if mutate else 0), result
        print(('MUTATE RED' if mutate else 'PASS') + ' sw fill (' + runner + ')')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = [args.runner] if args.runner else os.environ.get('HOST32_RUNNERS', 'qemu').split()
    with tempfile.TemporaryDirectory(prefix='gfx2-sw-fill-') as td:
        run(Path(td) / 'control', runners)
        if args.mutate:
            run(Path(td) / 'mutant', runners, True)


if __name__ == '__main__':
    main()
