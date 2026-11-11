#!/usr/bin/env python3
"""Check the PPC instruction publication loops in the compiled backend."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

CHECK = dict(name='ppc-exec-cache', tier=2, budget=60,
             covers=['arch/ppc/exec_cache.c', 'arch/ppc/exec_cache.h',
                     'tools/ppc/test_exec_cache.py'],
             inputs=['arch/ppc/ppc_oea.h', 'build/ppc-kboot.mk'])
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'arch/ppc/exec_cache.c'
PPC = Path('/home/hight/opt/cross-ppc/bin')


def require(cmd):
    p = subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=120)
    if p.returncode:
        raise RuntimeError(f'{cmd}: {p.stdout}')
    return p.stdout


def check_asm(dis):
    body = re.search(r'<arch_exec_publish>:(.*?)(?=\n[0-9a-f]+ <|\Z)', dis, re.S)
    if not body:
        return False
    text = body[1]
    # Optimized loops may be unrolled: verify alignment, line advances,
    # backward branches and the ordering of the two cache operations.
    sequence = re.search(r'\bdcbst\b.*?\b(?:hw)?sync\b(.*?)\b(?:hw)?sync\b.*?\bisync\b', text, re.S)
    if not sequence or not re.search(r'\bicbi\b', sequence[1]):
        return False
    before = text[:sequence.start(1)]
    return (len(re.findall(r'\bclrrwi\s+r?\d+,r?\d+,5\b', text)) >= 2
            and re.search(r'\baddi\s+r?\d+,r?\d+,32\b', before) is not None
            and re.search(r'\baddi\s+r?\d+,r?\d+,32\b', sequence[1]) is not None
            and re.search(r'\bb(?:dnz|ne)\s+[0-9a-f]+', before) is not None
            and re.search(r'\bbne\s+[0-9a-f]+', sequence[1]) is not None)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        mk = d / 'flags.mk'
        mk.write_text('include build/ppc-kboot.mk\nprint-flags:\n\t@echo $(KB_FLAGS)\n')
        flags = shlex.split(require(['make', '-s', '-f', str(mk), 'print-flags']))

        def compile_dis(src):
            f = d / 'source.c'
            obj = d / 'source.o'
            f.write_text(src)
            require([str(PPC / 'powerpc-elf-gcc'), *flags, '-c', str(f), '-o', str(obj)])
            undefined = require([str(PPC / 'powerpc-elf-nm'), '-u', str(obj)])
            if undefined.strip():
                raise RuntimeError('undefined symbols: ' + undefined)
            return require([str(PPC / 'powerpc-elf-objdump'), '-d', str(obj)])

        if not check_asm(compile_dis(original)):
            raise RuntimeError('cache instruction sequence or range loops mismatch')
        print('PPC compile, symbols, cache instruction sequence and range loops OK')
        if args.mutate:
            before = '__asm__ volatile("icbi 0,%0" : : "r"(line) : "memory");'
            if original.count(before) != 1:
                raise RuntimeError('mutation anchor: icbi')
            if check_asm(compile_dis(original.replace(before, '(void)line;'))):
                raise RuntimeError('survived: icbi')
            print('RED icbi')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as exc:
        print('test error:', exc)
        raise SystemExit(1)
