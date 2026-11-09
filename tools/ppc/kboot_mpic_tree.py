#!/usr/bin/env python3
"""Standalone ILP32 product-code OF snapshot test; outside make check."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32

CHECK = dict(name='ppc-kboot-mpic-tree', tier=2, budget=60,
             covers=['platform/powermac/mpic_tree.c', 'platform/powermac/mpic_tree.h',
                     'tools/ppc/kboot_mpic_tree.py', 'tools/ppc/kboot_mpic_tree_fake.c',
                     'platform/powermac/kboot_capture.c', 'platform/powermac/kboot_info.h',
                     'tools/ppc/kboot_capture_fake.c', 'platform/powermac/*.h',
                     'arch/ppc/*.h', 'include/types.h', 'include/callconv.h',
                     'lib/kstring.h', 'lib/kstring_c.c', 'tools/tests/host32.py'])
MUTATIONS = [
    ('range-direction', 'next=dest+(a-child);', 'next=child+(a-child);'),
    ('bar-choice', '(hi&0xff)!=PCI_BAR0', '(hi&0xff)!=0x14UL'),
    ('skip-parent', 'i+1<d->count', 'i+2<d->count'),
    ('length', 'n<MPIC_MIN_BYTES', '0'),
    ('order', '*order=PM_MPIC_MMIO_LE;', '*order=PM_MPIC_MMIO_BE;'),
    ('range-offset', 'next=dest+(a-child);', 'next=dest+a;'),
    ('relocatable', '!(hi&PCI_ABSOLUTE)', '0'),
]


def exercise(directory, source):
    exe = directory / 'capture'
    argv = ['gcc', '-std=gnu11', '-m32', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-fno-stack-protector',
            '-nostdlib', '-static', '-D__cdecl=',
            *['-I'+str(ROOT / p) for p in ('platform/powermac', 'arch/ppc', 'include', 'lib')],
            str(source), str(ROOT / 'platform/powermac/kboot_capture.c'),
            str(ROOT / 'tools/ppc/kboot_mpic_tree_fake.c'),
            str(ROOT / 'lib/kstring_c.c'), '-o', str(exe)]
    subprocess.run(argv, check=True, timeout=60)
    failed = []
    for case in range(28):
        result = host32.run([str(exe), str(case)], capture_output=True, timeout=10)
        if result.returncode:
            failed.append((case, result.returncode))
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    product = ROOT / 'platform/powermac/mpic_tree.c'
    with tempfile.TemporaryDirectory(prefix='kboot-capture-') as temp:
        directory = Path(temp)
        failures = exercise(directory, product)
        if failures:
            raise RuntimeError(f'baseline failures: {failures}')
        print('GREEN: 28 MPIC window cases (ILP32 product code)')
        if args.mutate:
            text = product.read_text()
            for name, old, new in MUTATIONS:
                if text.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor not unique')
                mutant = directory / 'mpic_tree.c'
                mutant.write_text(text.replace(old, new))
                failures = exercise(directory, mutant)
                if not failures:
                    raise RuntimeError(f'{name}: survived')
                print(f'RED: {name}: {failures}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
