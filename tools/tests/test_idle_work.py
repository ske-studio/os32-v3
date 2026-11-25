"""Exercise the resident cooperative work registry with a host IF seam."""
CHECK = dict(name='idle-work-host', tier=1,
    covers=['tools/tests/idle_io_sources.py', 'kernel/io_work.[ch]',
            'sys/kern/io_clock.h', 'kernel/io_policy.[ch]', 'sys/kern/exec/appslot.h',
            'kernel/idle_work.[ch]', 'sys/net/nic.h', 'sys/kern/irq/irq.h', 'sys/include/io.h',
            'build/sources/common.mk', 'build/sources/order.mk',
            'tools/tests/idle_work_host.c', 'tools/tests/test_idle_work.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3,
    basis=['AC-CTX-4'],
)

from idle_io_sources import idle_io_sources
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]


def main():
    original = (ROOT / 'sys/kern/idle_work.c').read_text()
    with tempfile.TemporaryDirectory(prefix='idle-work-') as td:
        exe = Path(td) / 'idle_work'

        def build(source, product=None):
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fno-pie', '-nostdlib', '-static', '-no-pie',
                            *include_flags(ROOT), '-DIDLE_WORK_SOURCE="' + str(source) + '"',
                            str(ROOT / 'tools/tests/idle_work_host.c'), *idle_io_sources(ROOT, td, product), '-o', str(exe)],
                           check=True)

        build(ROOT / 'sys/kern/idle_work.c')
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            assert host32.run([str(exe)], runner=runner).returncode == 0, runner
            print('PASS idle work (' + runner + ')', flush=True)
        if os.environ.get('MUTATE') == '1' or '--mutate' in sys.argv:
            mutants = [
                ('    idle_work_run();\n    if (!net_rx_recent(NET_RX_ACTIVE_MS)) _idle();', '    if (!net_rx_recent(NET_RX_ACTIVE_MS)) _idle();\n    idle_work_run();', 'wait order'),
                ('if (!net_rx_recent(NET_RX_ACTIVE_MS)) _idle();', '_idle();', 'RX sleep condition'),
                ('!_irq_enabled() || kctx_irq_depth != 0', 'kctx_irq_depth != 0', 'wait IF'),
                ('!_irq_enabled() || kctx_irq_depth != 0', '!_irq_enabled()', 'wait IRQ'),
                ('    if (!_irq_enabled()) return;', '    (void)_irq_enabled();', 'IF'),
                ('    if (kctx_irq_depth != 0) return;', '    (void)kctx_irq_depth;', 'depth'),
                ('work_count < IDLE_WORK_LIMIT', 'work_count <= IDLE_WORK_LIMIT', 'limit'),
            ]
            for old, new, label in mutants:
                assert original.count(old) == 1, label
                source = Path(td) / 'mutant.c'
                source.write_text(original.replace(old, new, 1))
                build(source)
                assert host32.run([str(exe)], runner='qemu').returncode != 0, label
                print('MUTATE RED ' + label, flush=True)
            product = (ROOT / 'sys/kern/io_work.c').read_text()
            for old, new, label in [
                ('if (running) return 0;', 'if (0) return 0;', 'reentry'),
                ('idle_work_compat_count() == 0 && !any_pending()', '!any_pending()', 'compat pending'),
                ('    idle_work_compat_run();', '    (void)0;', 'compat call'),
                ('if (idle_work_compat_count() == 0 && !any_pending()) return 0;',
                 'if (idle_work_compat_count() == 0 && !any_pending()) { u32 a,b,c; raw_read(&a,&b,&c); return 0; }', 'empty clock'),
            ]:
                assert product.count(old) == 1, label
                build(ROOT / 'sys/kern/idle_work.c', product.replace(old, new, 1))
                assert host32.run([str(exe)], runner='qemu').returncode != 0, label
                print('MUTATE RED ' + label, flush=True)



if __name__ == '__main__':
    main()


def run(ctx) -> int:
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_idle_work.py',
                            *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root, env=env, close_fds=False)
