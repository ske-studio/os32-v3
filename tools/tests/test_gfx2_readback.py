"""Exercise software readback with a real product file."""
import argparse
import os
from pathlib import Path
import tempfile
import host32
from mutpar import build_in_tree

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'gfx/v2/sw_readback.c'
OLD = 'u8 *dst = kbuf + y * op->pitch;'
NEW = 'u8 *dst = kbuf + y * op->sw * info.cpp;'


def run(folder, runners, mutate=False):
    exe = folder / 'readback-host'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
           '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
           '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
           *['-I' + str(ROOT / d) for d in ('.', 'include', 'sdk/include/os32',
             'arch/x86', 'platform/pc98', 'mm', 'lib', 'gfx/v2')],
           str(ROOT / 'tools/tests/gfx2_readback_host.c'), str(ROOT / SOURCE), '-o', str(exe)]
    edits = {}
    if mutate:
        source = (ROOT / SOURCE).read_text()
        assert source.count(OLD) == 1
        edits[SOURCE] = source.replace(OLD, NEW)
    build_in_tree(ROOT, folder, edits, [cmd])
    for runner in runners:
        result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
        assert result.returncode == (3 if mutate else 0), result
        print(('MUTATE RED' if mutate else 'PASS') + ' readback (' + runner + ')')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = [args.runner] if args.runner else os.environ.get('HOST32_RUNNERS', 'qemu').split()
    with tempfile.TemporaryDirectory(prefix='gfx2-readback-') as td:
        run(Path(td) / 'control', runners)
        if args.mutate:
            run(Path(td) / 'mutant', runners, True)


if __name__ == '__main__':
    main()
