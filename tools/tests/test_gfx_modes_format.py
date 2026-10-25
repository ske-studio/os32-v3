"""G2: real format/mode headers, layout overflow and default/legacy selection."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from mutpar import build_in_tree

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'include/gfx_format.h'
OLD = 'width / BITS_PER_BYTE + (width % BITS_PER_BYTE != 0)'
NEW = 'width / 2'


def run(folder, runners, mutate=False):
    exe = folder / 'format-host'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
           '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
           '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
           *['-I' + str(ROOT / d) for d in
             ('.', 'include', 'sdk/include/os32', 'arch/x86', 'platform/pc98')],
           str(ROOT / 'tools/tests/gfx_modes_format_host.c'), '-o', str(exe)]
    edits = {}
    if mutate:
        source = (ROOT / SOURCE).read_text()
        assert source.count(OLD) == 1
        edits[SOURCE] = source.replace(OLD, NEW)
    # The control uses the same copied tree/build path as the mutant.
    build_in_tree(ROOT, folder, edits, [cmd])
    for runner in runners:
        result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
        expected_rc, expected_output = (1, 'FAIL row == rows[i]\n') if mutate else (0, '')
        assert result.returncode == expected_rc and result.stdout == expected_output, result
        print(('MUTATE RED' if mutate else 'PASS') + ' format/mode/layout (' + runner + ')')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = [args.runner] if args.runner else os.environ.get('HOST32_RUNNERS', 'qemu').split()
    with tempfile.TemporaryDirectory(prefix='gfx-modes-format-') as td:
        folder = Path(td)
        run(folder / 'control', runners)
        if args.mutate:
            run(folder / 'mutant', runners, True)


if __name__ == '__main__':
    main()
