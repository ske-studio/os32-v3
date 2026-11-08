"""Resident GeForce tick callback lifetime."""
CHECK = dict(name='gf1-tick-host', tier=3,
    covers=['drivers/video/geforce/geforce_tick.[ch]', 'kernel/irq/tick.h',
            'platform/pc98/tick_order.h', 'tools/tests/gf1_tick_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=2.6)

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'drivers/video/geforce/geforce_tick.c'

def main():
    with tempfile.TemporaryDirectory(prefix='gf1-tick-') as td:
        exe = Path(td) / 'test'
        def run(source, runner):
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fno-pie', '-nostdlib', '-static', '-no-pie',
                            *include_flags(ROOT), '-DGF_TICK_SOURCE="' + str(source) + '"',
                            str(ROOT / 'tools/tests/gf1_tick_host.c'), '-o', str(exe)], check=True)
            return host32.run([str(exe)], runner=runner).returncode
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            assert run(SOURCE, runner) == 0
            print('PASS callback registration, ticks, unbind and rebind', flush=True)
        if '--mutate' in sys.argv:
            original = SOURCE.read_text()
            old = 'tick_poll = 0;'
            assert original.count(old) == 1
            mutant = Path(td) / 'mutant.c'
            # A stale callback simulates losing the unbind operation.
            mutant.write_text(original.replace(old, '(void)poll;', 1))
            assert run(mutant, 'qemu') != 0
            print('MUTATE RED callback survives unbind', flush=True)

if __name__ == '__main__':
    main()

def run(ctx):
    import shlex
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf1_tick.py',
                            *shlex.split('--mutate' if ctx.mutate else '')],
                           cwd=ctx.root, env=env, close_fds=False)
