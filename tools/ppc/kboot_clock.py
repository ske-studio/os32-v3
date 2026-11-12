#!/usr/bin/env python3
"""ILP32 product timebase tests; standalone, outside make check."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32
CHECK = dict(name='ppc-kboot-clock', tier=2, budget=60,
             covers=['init/ppc_kernel.c', 'platform/powermac/clock.c', 'platform/powermac/kboot_info.h',
                     'tools/ppc/kboot_clock*', 'lib/divdi3.c'],
             inputs=['include/types.h', 'include/callconv.h', 'include/clock_source.h',
                     'include/platform_timer.h', 'platform/powermac/ofw.h',
                     'platform/powermac/ppc_layout.h', 'arch/ppc/ppc_boot.h',
                     'arch/ppc/ppc_pmap.h', 'arch/ppc/ppc_oea.h',
                     'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/host32.py'])
MUTATIONS = [
    ('retry-removed', 'while (upper != again)', 'while (0 && upper != again)', 6),
    ('upper-comparison', 'while (upper != again)', 'while (upper == again)', 3),
    ('rounding', '(delta / hz) * 1000000', '(delta / hz) * 1000000 + 1', 6),
    ('ready-removed', 'if (!clock_state.ready)', 'if (0)', 4),
    ('range-removed', 'bound_hz < 1000000 || bound_hz > 1000000000', '0', 7),
    ('high-word', '*hi = (u32)(us >> 32);', '*hi = 0;', 6),
]


def exercise(directory, source):
    (directory / 'clock.c').write_text(source)
    exe = directory / 'clock-test'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie',
                    '-fno-stack-protector', '-nostdlib', '-static', '-D__cdecl=',
                    '-I'+str(directory),
                    *['-I'+str(ROOT / p) for p in ('include', 'platform/powermac',
                                                  'arch/ppc', 'sdk/include/os32')],
                    str(ROOT / 'tools/ppc/kboot_clock_fake.c'),
                    str(ROOT / 'lib/divdi3.c'), '-o', str(exe)], check=True, timeout=60)
    try:
        return host32.run([str(exe)], capture_output=True, timeout=2).returncode
    except subprocess.TimeoutExpired:
        raise RuntimeError('test failure: runtime timeout; not RED') from None


def rate_test(directory, source, mutant=False):
    start = source.index('    u32 want = (u32)((tb1_b - tb0_a)')
    end = source.index('    kprintf(KBOOT_ATTR, "KBOOT CLOCK', start)
    block = source[start:end]
    old = '(unsigned long long)got + 1 < lower ||\n        (unsigned long long)got > upper + 1'
    if block.count(old) != 1:
        raise RuntimeError('rate mutation anchor must be unique')
    if mutant:
        block = block.replace(old, '((void)got, (void)lower, (void)upper, 0)')
    program = r"""
#include <stdint.h>
typedef uint32_t u32;
struct info { u32 tb_hz; };
static int check(unsigned long long tb0_a, unsigned long long tb0_b,
                 unsigned long long tb1_a, unsigned long long tb1_b, u32 sample) {
    struct info data = {1000000}, *info = &data;
    u32 us0 = 0, us_lo = sample;
#define kboot_stop(reason) return 1
""" + block + r"""
    return 0;
}
int main(void) {
    if (check(0, 1000, 31000, 32000, 31000)) return 1;
    if (check(0, 1000, 31000, 32000, 60000) != 1) return 2;
    if (check(0, 1000, 31000, 32000, 10000) != 1) return 3;
    if (check(0, 0, 9000, 9000, 9000) != 1) return 4;
    if (check(0, 1000, 31000, 32000, 29999)) return 5;
    if (check(0, 1000, 31000, 32000, 32001)) return 6;
    return 0;
}
"""
    path = directory / 'rate.c'
    path.write_text(program)
    exe = directory / 'rate-test'
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    str(path), '-o', str(exe)], check=True, timeout=60)
    return subprocess.run([str(exe)], timeout=2).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'platform/powermac/clock.c').read_text()
    with tempfile.TemporaryDirectory(prefix='kboot-clock-') as temp:
        directory = Path(temp)
        rate_source = (ROOT / 'init/ppc_kernel.c').read_text()
        if rate_test(directory, rate_source) != 0:
            raise RuntimeError('rate baseline failed')
        print('GREEN: bracketed clock rate')
        if args.mutate:
            result = rate_test(directory, rate_source, mutant=True)
            if result != 2:
                raise RuntimeError(f'rate mutation: expected 2, got {result}')
            print('RED: rate comparison removed: expected=2 actual=2')
        result = exercise(directory, source)
        if result:
            raise RuntimeError(f'baseline: {result}')
        print('GREEN: ILP32 product TB retry/wrap/60 days/range/readiness')
        if args.mutate:
            for name, old, new, expected in MUTATIONS:
                if source.count(old) != 1:
                    raise RuntimeError(f'{name}: nonunique anchor')
                result = exercise(directory, source.replace(old, new))
                if result == 0:
                    raise RuntimeError(f'SURVIVED: {name}: expected={expected} actual=0')
                if result != expected:
                    raise RuntimeError(f'test failure: {name}: expected={expected} actual={result}; not RED')
                print(f'RED: {name}: expected={expected} actual={result}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
