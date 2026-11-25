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
    'sys/platform/powermac/via_tree.c', 'sys/platform/powermac/via_window.h',
    'sys/platform/powermac/kboot_reserve.c', 'sys/platform/powermac/kboot_capture.c',
    'sys/platform/powermac/gmac_tree.c',
    'sys/platform/powermac/kboot_info.h', 'sys/platform/powermac/platform_kboot_layout.h',
    'tools/ppc/kboot_reserve.py', 'tools/ppc/kboot_reserve_fake.c',
    'tools/ppc/kboot_capture_fake.c', 'sys/platform/powermac/ofw.h',
    'sys/platform/powermac/platform_of_layout.h', 'sys/arch/ppc/ppc_boot.h',
    'sys/arch/ppc/ppc_pmap.h', 'sys/arch/ppc/ppc_oea.h', 'sys/include/memmap.h',
    'sys/include/types.h', 'sys/include/callconv.h', 'sys/kern/kstring.h',
    'sys/kern/kstring_c.c', 'tools/tests/host32.py', 'tools/ppc/kboot_layout.py', 'tools/ppc/kboot_root_image.py'])
MUTATIONS = [
    ('ignore-hole', 'if (r->v[i].start>first) return 0;', 'if (0) return 0;', {17: (77, 'CR0'), 1: (77, 'CR0'), 6: (77, 'CR0'), 7: (77, 'CR0'), 11: (77, 'CR0'), 15: (77, 'CR0')}),
    ('image-unowned', 'if (r->start<end && r->start+r->size>first)',
     'if (0 && r->start<end && r->start+r->size>first)', {2: (77, 'CR0')}),
    ('hpt-align-down', 'start=(start+PM_HPT_BYTES-1)&~(PM_HPT_BYTES-1);',
     'start=start&~(PM_HPT_BYTES-1);', {3: (77, 'CR0'), 5: (78, 'CR0')}),
    ('window-end', '!covered(&info->available,window,PM_WINDOW_END)',
     '!covered(&info->available,window,PM_WINDOW_END-1)', {6: (77, 'CR0')}),
    ('skip-vector', 'if (!covered(&info->ram,0,PM_VECTOR_END))', 'if (0)', {7: (77, 'CR0')}),
    ('no-image-slack', 'r->start-end<=PM_IMAGE_SLACK', '0', {14: (77, 'CR5')}),
    ('unbounded-slack', 'r->start-end<=PM_IMAGE_SLACK', '1', {11: (77, 'CR0'), 15: (77, 'CR0')}),
    ('stale-snapshot', 'pm_kboot_refresh_available(info);', '(void)info;', {11: (77, 'CR0')}),
]


def exercise(directory, source, expected, end=0x300000, cases=range(18)):
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
                    '-fno-stack-protector', '-nostdlib', '-nostdinc', '-static', f'-DRESIDENT_END={end}U', '-D__cdecl=',
                    *['-I'+str(ROOT / p) for p in ('platform/powermac', 'arch/ppc', 'include', 'lib')],
                    str(source), str(ROOT / 'sys/platform/powermac/kboot_capture.c'),
                    str(ROOT / 'sys/platform/powermac/gmac_tree.c'), str(ROOT / 'sys/platform/powermac/via_tree.c'),
                    str(fixture), str(ROOT / 'sys/kern/kstring_c.c'),
                    '-Wl,--defsym,__kernel_image_start=0x100000',
                    f'-Wl,--defsym,__kernel_resident_end={end}', '-o', str(exe)],
                   check=True, timeout=60)
    actual = {}
    for case in cases:
        result = host32.run([str(exe), str(case)], capture_output=True, timeout=10)
        marks = result.stdout.decode('ascii')
        if result.returncode or case in expected:
            actual[case] = (result.returncode, marks)
        elif marks != ('CR' + str([0, 5, 3, 6, 5, 0, 5, 4, 0, 2, 6, 5, 1, 0, 0, 5, 0, 5][case])
                       if case != 8 else '') or result.stderr:
            raise RuntimeError(f'case {case}: unexpected marks {marks!r} or stderr {result.stderr!r}')
        if result.stderr:
            raise RuntimeError(f'case {case}: unexpected stderr {result.stderr!r}')
    print(f'expected={expected}, actual={actual}')
    if actual != expected:
        raise RuntimeError('case/exit/reached-marker mismatch')



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--product', type=Path, help='use linked product layout in OF fixtures')
    args = parser.parse_args()
    product = ROOT / 'sys/platform/powermac/kboot_reserve.c'
    with tempfile.TemporaryDirectory(prefix='kboot-reserve-') as temp:
        directory = Path(temp)
        exercise(directory, product, {})
        if args.product:
            from kboot_layout import report_product
            end = report_product(args.product)
            exercise(directory, product, {}, end, (16, 17))
        print('GREEN: 18 ILP32 reserve/capture cases')
        if args.mutate:
            original = product.read_text()
            for name, old, new, expected in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor not unique')
                mutant = directory / 'kboot_reserve.c'
                mutant.write_text(original.replace(old, new))
                print(f'mutation {name}: ', end='', flush=True)
                exercise(directory, mutant, expected)
                print(f'RED: {name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
