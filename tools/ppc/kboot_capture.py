#!/usr/bin/env python3
"""Standalone ILP32 product-code OF snapshot test; outside make check."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32

CHECK = dict(name='ppc-kboot-capture', tier=2, budget=60,
             covers=['platform/powermac/kboot_capture.c', 'platform/powermac/kboot_info.h',
                     'tools/ppc/kboot_capture.py', 'tools/ppc/kboot_capture_fake.c',
                     'platform/powermac/ofw.h', 'platform/powermac/ppc_layout.h',
                     'arch/ppc/ppc_boot.h', 'arch/ppc/ppc_pmap.h', 'arch/ppc/ppc_oea.h',
                     'include/types.h', 'include/callconv.h', 'lib/kstring.h',
                     'lib/kstring_c.c', 'tools/tests/host32.py'])
MUTATIONS = [
    ('ignore-hole', 'r.start<=out->v[kept-1].start+out->v[kept-1].size', '1'),
    ('cell-count', 'unsigned stride=(ac+sc)*PM_CELL_BYTES;', 'unsigned stride=2*PM_CELL_BYTES;'),
    ('overflow', 'size>0xffffffffUL-start', '0'),
    ('limit-off-by-one', 'count>PM_REGION_COUNT', 'count>=PM_REGION_COUNT'),
    ('borrow-property', 'return &info;', 'return (const struct pm_kboot_info *)property;'),
    ('high-cell', '(ac==2 && be32(p))', '(0)'),
]


def exercise(directory, source):
    exe = directory / 'capture'
    argv = ['gcc', '-std=gnu11', '-m32', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-fno-stack-protector',
            '-nostdlib', '-static', '-D__cdecl=',
            *['-I'+str(ROOT / p) for p in ('platform/powermac', 'arch/ppc', 'include', 'lib')],
            str(source), str(ROOT / 'tools/ppc/kboot_capture_fake.c'),
            str(ROOT / 'lib/kstring_c.c'), '-o', str(exe)]
    subprocess.run(argv, check=True, timeout=60)
    failed = []
    for case in range(19):
        result = host32.run([str(exe), str(case)], capture_output=True, timeout=10)
        if result.returncode:
            failed.append((case, result.returncode))
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    product = ROOT / 'platform/powermac/kboot_capture.c'
    with tempfile.TemporaryDirectory(prefix='kboot-capture-') as temp:
        directory = Path(temp)
        failures = exercise(directory, product)
        if failures:
            raise RuntimeError(f'baseline failures: {failures}')
        print('GREEN: 19 firmware-tree cases (ILP32 product code)')
        if args.mutate:
            text = product.read_text()
            for name, old, new in MUTATIONS:
                if text.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor not unique')
                mutant = directory / 'kboot_capture.c'
                mutant.write_text(text.replace(old, new))
                failures = exercise(directory, mutant)
                if not failures:
                    raise RuntimeError(f'{name}: survived')
                print(f'RED: {name}: {failures}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
