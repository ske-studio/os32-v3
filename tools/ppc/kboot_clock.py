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
             covers=['platform/powermac/clock.c', 'platform/powermac/kboot_info.h',
                     'tools/ppc/kboot_clock*', 'lib/divdi3.c'],
             inputs=['include/types.h', 'include/callconv.h', 'include/clock_source.h',
                     'include/platform_timer.h', 'platform/powermac/ofw.h',
                     'platform/powermac/ppc_layout.h', 'arch/ppc/ppc_boot.h',
                     'arch/ppc/ppc_pmap.h', 'arch/ppc/ppc_oea.h',
                     'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/host32.py'])
MUTATIONS = [
    ('retry-removed', 'while (upper != again)', 'while (0 && upper != again)'),
    ('upper-comparison', 'while (upper != again)', 'while (upper == again)'),
    ('rounding', '(delta / hz) * 1000000', '(delta / hz) * 1000000 + 1'),
    ('ready-removed', 'if (!clock_state.ready)', 'if (0)'),
    ('range-removed', 'bound_hz < 1000000 || bound_hz > 1000000000', '0'),
    ('high-word', '*hi = (u32)(us >> 32);', '*hi = 0;'),
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
        return 'runtime timeout'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'platform/powermac/clock.c').read_text()
    with tempfile.TemporaryDirectory(prefix='kboot-clock-') as temp:
        directory = Path(temp)
        result = exercise(directory, source)
        if result:
            raise RuntimeError(f'baseline: {result}')
        print('GREEN: ILP32 product TB retry/wrap/60 days/range/readiness')
        if args.mutate:
            for name, old, new in MUTATIONS:
                if source.count(old) != 1:
                    raise RuntimeError(f'{name}: nonunique anchor')
                result = exercise(directory, source.replace(old, new))
                if not result:
                    raise RuntimeError(f'{name}: survived')
                print(f'RED: {name}: {result}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
