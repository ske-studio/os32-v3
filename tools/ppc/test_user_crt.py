#!/usr/bin/env python3
"""Compile the PPC USER CRT and inspect its entry sequence."""
CHECK = dict(name='ppc-user-crt', tier=2, budget=60,
             covers=['sdk/crt/ppc/crt0.S', 'sdk/crt/ppc/start.c',
                     'tools/ppc/test_user_crt.py'],
             inputs=['build/ppc-kboot.mk', 'sdk/include/os32api.h'], needs=[])
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BIN = Path('/home/hight/opt/cross-ppc/bin')


def command(args):
    return subprocess.run(args, cwd=ROOT, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'sdk/crt/ppc/crt0.S').read_text()
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        mk = d / 'flags.mk'
        mk.write_text('include build/ppc-kboot.mk\nprint-flags:\n\t@echo $(KB_FLAGS)\n')
        flags = shlex.split(command(['make', '-s', '-f', str(mk), 'print-flags']))
        gcc = str(BIN / 'powerpc-elf-gcc')
        obj = d / 'start.o'
        command([gcc, *flags, '-c', 'sdk/crt/ppc/start.c', '-o', str(obj)])
        undefined = command([str(BIN / 'powerpc-elf-nm'), '-u', str(obj)])
        print('start.c nm -u:', undefined.strip())
        if not re.search(r'\bmain$', undefined, re.M):
            raise RuntimeError('main missing')

        def inspect(text):
            asm = d / 'crt0.S'
            asm.write_text(text)
            command([gcc, *flags, '-c', str(asm), '-o', str(obj)])
            symbols = command([str(BIN / 'powerpc-elf-nm'), str(obj)])
            dis = command([str(BIN / 'powerpc-elf-objdump'), '-dr', str(obj)])
            print('crt0 nm -u:', command([str(BIN / 'powerpc-elf-nm'), '-u', str(obj)]).strip())
            instructions = re.findall(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+){4}\s*(.*)$', dis, re.M)
            return (re.search(r'\bT _start$', symbols, re.M) is not None
                    and len(instructions) == 2
                    and re.match(r'stwu\s+r?1,-16\(r?1\)', instructions[0]) is not None
                    and re.match(r'bl\s', instructions[1]) is not None
                    and 'os32_start_c' in dis)

        if not inspect(source):
            raise RuntimeError('entry sequence mismatch')
        print('PPC USER CRT compile and entry OK')
        if args.mutate:
            old = 'stwu 1,-16(1)'
            if source.count(old) != 1:
                raise RuntimeError('mutation anchor mismatch')
            if inspect(source.replace(old, 'stwu 3,-16(1)')):
                raise RuntimeError('mutation survived')
            print('RED argument register clobber (1)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
