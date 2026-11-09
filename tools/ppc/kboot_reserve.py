#!/usr/bin/env python3
"""ILP32 product reserve + capture with B1 firmware fake; outside make check."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32
CHECK = dict(name='ppc-kboot-reserve', tier=2, budget=60, covers=[
    'platform/powermac/kboot_reserve.c', 'platform/powermac/kboot_capture.c',
    'platform/powermac/kboot_info.h', 'platform/powermac/kboot_layout.h',
    'tools/ppc/kboot_reserve.py', 'tools/ppc/kboot_reserve_fake.c',
    'tools/ppc/kboot_capture_fake.c', 'platform/powermac/ofw.h',
    'platform/powermac/ppc_layout.h', 'arch/ppc/ppc_boot.h',
    'arch/ppc/ppc_pmap.h', 'arch/ppc/ppc_oea.h', 'include/memmap.h',
    'include/types.h', 'include/callconv.h', 'lib/kstring.h',
    'lib/kstring_c.c', 'tools/tests/host32.py'])
MUTATIONS = [
    ('ignore-hole', 'if (r->v[i].start>first) return 0;', 'if (0) return 0;'),
    ('image-unowned', 'if (r->start<end && r->start+r->size>first)',
     'if (0 && r->start<end && r->start+r->size>first)'),
    ('hpt-align-down', 'start=(start+PM_HPT_BYTES-1)&~(PM_HPT_BYTES-1);',
     'start=start&~(PM_HPT_BYTES-1);'),
    ('window-end', '!covered(&info->available,end,PM_WINDOW_END)',
     '!covered(&info->available,end,PM_WINDOW_END-1)'),
    ('skip-vector', 'if (!covered(&info->ram,0,PM_VECTOR_END))', 'if (0)'),
    ('stale-snapshot', 'pm_kboot_refresh_available(info);', '(void)info;'),
]


def exercise(directory, source):
    fake = (ROOT / 'tools/ppc/kboot_capture_fake.c').read_text()
    anchor = 'static void test(void)\n{'
    if fake.count(anchor) != 1:
        raise RuntimeError('B1 fake entry anchor changed')
    fake = fake.replace(anchor, 'static void reserve_test(void);\n' + anchor + '\n    reserve_test();')
    fixture = directory / 'fixture.c'
    fixture.write_text(fake + (ROOT / 'tools/ppc/kboot_reserve_fake.c').read_text())
    exe = directory / 'reserve'
    subprocess.run(['gcc', '-std=gnu11', '-m32', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie',
                    '-fno-stack-protector', '-nostdlib', '-static', '-D__cdecl=',
                    *['-I'+str(ROOT / p) for p in ('platform/powermac', 'arch/ppc', 'include', 'lib')],
                    str(source), str(ROOT / 'platform/powermac/kboot_capture.c'),
                    str(fixture), str(ROOT / 'lib/kstring_c.c'),
                    '-Wl,--defsym,__kernel_image_start=0x100000',
                    '-Wl,--defsym,__kernel_resident_end=0x300000', '-o', str(exe)],
                   check=True, timeout=60)
    failures = []
    for case in range(13):
        result = host32.run([str(exe), str(case)], capture_output=True, timeout=10)
        if result.returncode == 77:
            failures.append(case)
        elif result.returncode:
            raise RuntimeError(f'case {case}: unexpected exit {result.returncode}')
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    product = ROOT / 'platform/powermac/kboot_reserve.c'
    with tempfile.TemporaryDirectory(prefix='kboot-reserve-') as temp:
        directory = Path(temp)
        failures = exercise(directory, product)
        if failures:
            raise RuntimeError(f'baseline mismatch: {failures}')
        print('GREEN: 13 ILP32 reserve/capture cases')
        if args.mutate:
            original = product.read_text()
            for name, old, new in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor not unique')
                mutant = directory / 'kboot_reserve.c'
                mutant.write_text(original.replace(old, new))
                failures = exercise(directory, mutant)
                if not failures:
                    raise RuntimeError(f'{name}: survived')
                print(f'RED: {name}: expected mismatch exit 77, cases {failures}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
