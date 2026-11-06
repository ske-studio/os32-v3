"""F0 resident foreground dispatch: direct product host and runtime mutants."""
CHECK = dict(name='geforce-fgpoll-host', tier=1, needs=['make:all'], budget=5.0,
             covers=['tools/tests/test_geforce_fgpoll.py',
                     'tools/tests/geforce_fgpoll_host.c', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'build/config.mk',
                     'drivers/video/geforce/**', 'hwdev/gf/**', 'include/**',
                     'gfx/gfx_internal.h', 'gfx/palette.h', 'mm/pgalloc.h',
                     'fs/vfs.h', 'exec/appslot.h', 'lib/serial_diag.h',
                     'arch/x86/kernel/idt.h',
                     'kernel/module/*.h', 'lib/kstring.h',
                     'drivers/video/pc98/pc98_route.h',
                     'build/out/geforce/geforce_imports.h'])

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'drivers/video/geforce/geforce_module.c'
MUTATIONS = [
    ('deadline-minimum', 'if (deadlines[i] && deadlines[i]<next)',
     'if (0 && deadlines[i] && deadlines[i]<next)'),
    ('deadline-pending', 'return heat_pending || !foreground_deadline_valid ||',
     'return false || !foreground_deadline_valid ||'),
    ('heat-consume', '    heat_stop_consume();', '    if (0) heat_stop_consume();'),
    ('heat-latch', '    heat_bridge.latched=true;', '    heat_bridge.latched=false;'),
    ('heat-notify', '    geforce_display_failed();', '    if (0) geforce_display_failed();'),
    ('heat-abort', '    (void)appslot_abort_request();', '    if (0) (void)appslot_abort_request();'),
    ('heat-sync', 'int sync_rc=vfs_sync();', 'int sync_rc=0;'),
    ('heat-clear', 'if (!rc) heat_bridge.latched=false;', 'if (0) heat_bridge.latched=false;'),
    ('foreground-dispatch', 'if (ops && ops->service_poll)',
     'if (0 && ops && ops->service_poll)'),
    ('foreground-reentry', 'if (foreground_poll_busy) return;',
     'if (0 && foreground_poll_busy) return;'),
]


def run_case(folder, label, replacement=None):
    source = SOURCE.read_text()
    if replacement:
        old, new = replacement
        assert source.count(old) == 1, label
        source = source.replace(old, new, 1)
    source=source.replace('_disable();', 'host_disable();').replace('_halt();', 'host_halt();')
    # Rebase quoted sibling includes for the temporary product translation unit.
    source = re.sub(r'#include "([^"\n]+)"',
                    lambda m: '#include "' + str((SOURCE.parent / m[1]).resolve()) + '"'
                    if (SOURCE.parent / m[1]).exists() else m[0], source)
    product = folder / 'module.c'
    product.write_text(source)
    fixture = folder / 'host.c'
    fixture.write_text((ROOT / 'tools/tests/geforce_fgpoll_host.c').read_text().replace(
        '../../drivers/video/geforce/geforce_module.c', str(product)))
    exe = folder / 'host'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-ffreestanding',
                    '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__',
                    '-ffunction-sections', '-fdata-sections', *include_flags(ROOT),
                    '-I' + str(ROOT / 'build/out/geforce'),
                    '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                    str(fixture), '-o', str(exe)], cwd=ROOT, check=True)
    result = host32.run([str(exe)], capture_output=True, timeout=10)
    host32.report_signal(result.returncode)
    expected = 1 if replacement else 0
    assert result.returncode == expected, (label, result.returncode, result.stderr)
    print(('MUTATE runtime RED: ' if replacement else 'GREEN: ') + label, flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf-fgpoll-') as td:
        folder = Path(td)
        run_case(folder, 'foreground')
        if args.mutate:
            for name, old, new in MUTATIONS:
                run_case(folder, name, (old, new))
                run_case(folder, name + '-restored')


def run(ctx) -> int:
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = [sys.executable, '-B', 'tools/tests/test_geforce_fgpoll.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
