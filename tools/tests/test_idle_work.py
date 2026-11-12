"""Exercise the resident cooperative work registry with a host IF seam."""
CHECK = dict(name='idle-work-host', tier=1,
    covers=['kernel/idle_work.[ch]', 'kernel/irq/irq.h', 'include/io.h',
            'build/sources/common.mk', 'build/sources/order.mk',
            'tools/tests/idle_work_host.c', 'tools/tests/test_idle_work.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3)

import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]


def main():
    original = (ROOT / 'kernel/idle_work.c').read_text()
    with tempfile.TemporaryDirectory(prefix='idle-work-') as td:
        exe = Path(td) / 'idle_work'

        def build(source):
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fno-pie', '-nostdlib', '-static', '-no-pie',
                            *include_flags(ROOT), '-DIDLE_WORK_SOURCE="' + str(source) + '"',
                            str(ROOT / 'tools/tests/idle_work_host.c'), '-o', str(exe)],
                           check=True)

        build(ROOT / 'kernel/idle_work.c')
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            assert host32.run([str(exe)], runner=runner).returncode == 0, runner
            print('PASS idle work (' + runner + ')', flush=True)
        if '--mutate' in sys.argv:
            mutants = [
                ('    if (!_irq_enabled()) return;', '    (void)_irq_enabled();', 'IF'),
                ('    if (kctx_irq_depth != 0) return;', '    (void)kctx_irq_depth;', 'depth'),
                ('    if (running) return;', '    (void)running;', 'reentry'),
                ('work_count < IDLE_WORK_LIMIT', 'work_count <= IDLE_WORK_LIMIT', 'limit'),
            ]
            for old, new, label in mutants:
                assert original.count(old) == 1, label
                source = Path(td) / 'mutant.c'
                source.write_text(original.replace(old, new, 1))
                build(source)
                assert host32.run([str(exe)], runner='qemu').returncode != 0, label
                print('MUTATE RED ' + label, flush=True)


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_idle_work.py',
                            *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root, env=env, close_fds=False)
