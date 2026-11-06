"""B03: real gf5 diagnostic dispatch with module boundary callbacks."""
CHECK = dict(name='gf5-gpio-diag', tier=1, needs=[], budget=20.0,
             covers=['kdebug/gf5_test.c', 'kdebug/gf5_test.h',
                     'kdebug/gf_command_wire.h', 'hwdev/gf/**',
                     'drivers/video/geforce/geforce_module.h',
                     'kdebug/gfinit_decode.h', 'kdebug/gfinit_pll.h', 'lib/serial_diag.h',
                     'include/types.h', 'tools/tests/gf5_gpio_diag_host.c',
                     'tools/tests/serial_diag_host.h', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
                     'tools/tests/test_gf5_gpio_diag.py'])

import argparse
from pathlib import Path
import subprocess
import tempfile
import host32
import mutpar
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'kdebug/gf5_test.c'
# Replace only resolved DCB logs; compilation must still succeed.
ANCHOR = 'rc = o->gpio_find(owner, (u8)a[0], &line, log);'
MUTANT = ANCHOR + '\n        log[0] = 0; log[1] = 1;'


def command(exe):
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
            '-Wextra', '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
            '-D__KERNEL_BUILD__', *include_flags(ROOT),
            str(ROOT / 'tools/tests/gf5_gpio_diag_host.c'),
            str(ROOT / PRODUCT), '-o', str(exe)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf5-gpio-diag-') as td:
        exe = Path(td) / 'gpio'
        cmd = command(exe)
        subprocess.run(cmd, cwd=ROOT, check=True)
        if host32.run([str(exe)], cwd=ROOT).returncode:
            return 1
        print('PASS B03: runtime GREEN', flush=True)
        if args.mutate:
            source = (ROOT / PRODUCT).read_text()
            if source.count(ANCHOR) != 1:
                raise RuntimeError('B03 mutation anchor is not unique')
            tree = mutpar.build_in_tree(ROOT, Path(td) / 'mutant',
                                       {PRODUCT: source.replace(ANCHOR, MUTANT)}, [cmd])
            rc = host32.run([str(exe)], cwd=tree).returncode
            if rc <= 0:
                raise RuntimeError(f'B03 fixed-log mutation did not fail at runtime: {rc}')
            print(f'PASS B03 fixed-log: runtime RED rc={rc}', flush=True)
            subprocess.run(cmd, cwd=ROOT, check=True)
            if host32.run([str(exe)], cwd=ROOT).returncode:
                return 1
            print('PASS B03: restored runtime GREEN', flush=True)
    return 0


def run(ctx):
    import os
    import sys
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    raise SystemExit(main())
