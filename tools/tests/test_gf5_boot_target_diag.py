"""Boot-target A/B foreground commands, selection, availability and owner checks."""
CHECK = dict(name='gf5-boot-target-diag', tier=1, needs=[], budget=4.1,
             covers=['kdebug/gf5_test.c', 'kdebug/gf5_test.h',
                     'kdebug/gf_command_wire.h', 'hwdev/gf/**',
                     'drivers/video/geforce/geforce_module.h',
                     'kdebug/gfinit_decode.h', 'kdebug/gfinit_pll.h', 'lib/serial_diag.h', 'lib/crc32_core.inc',
                     'include/types.h', 'tools/tests/gf5_boot_target_diag_host.c', 'tools/tests/gf5_observe_host.c',
                     'tools/tests/serial_diag_host.h', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
                     'tools/tests/test_gf5_boot_target_diag.py'])

import argparse
from pathlib import Path
import subprocess
import tempfile
import host32
import mutpar
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'kdebug/gf5_test.c'
MUTATIONS = [
    ('slot', 'diagnostic.image[a[0]]) { rc = GF5_EBUSY;',
     'diagnostic.image[0]) { rc = GF5_EBUSY;'),
    ('first-different', 'if (same_clock(&before, &entry.clock)) continue;',
     'if (false && same_clock(&before, &entry.clock)) continue;'),
    ('restore-slot', 'rc = o->state_restore(o->ctx, diagnostic.image[a[0]]);',
     'rc = o->state_restore(o->ctx, diagnostic.image[0]);'),
]


def command(exe):
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
            '-Wextra', '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
            '-D__KERNEL_BUILD__', '-ffunction-sections', '-fdata-sections',
            '-Wl,--gc-sections', *include_flags(ROOT),
            str(ROOT / 'tools/tests/gf5_boot_target_diag_host.c'),
            str(ROOT / 'hwdev/gf/state.c'),
            str(ROOT / PRODUCT), '-o', str(exe)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf5-boot-target-diag-') as td:
        exe = Path(td) / 'observe'
        cmd = command(exe)
        subprocess.run(cmd, cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT)
        assert result.returncode == 0, ('diagnostic runtime', result.returncode)
        print('boot-target-diag runtime GREEN', flush=True)
        if args.mutate:
            source = (ROOT / PRODUCT).read_text()
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, (name, source.count(old))
                tree = mutpar.build_in_tree(ROOT, Path(td) / name,
                    {PRODUCT: source.replace(old, new)}, [cmd])
                rc = host32.run([str(exe)], cwd=tree).returncode
                assert rc > 0, (name, rc)
                print(name + ' runtime RED', flush=True)
                subprocess.run(cmd, cwd=ROOT, check=True)
                assert host32.run([str(exe)], cwd=ROOT).returncode == 0
                print(name + ' restored GREEN', flush=True)
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
