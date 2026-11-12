"""A2-t2: deferred NE2K device work across every disabled-IF entry."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ne2k-bh-host', tier=4, needs=[], budget=4,
    covers=['drivers/net/ne2000/ne2000.c', 'drivers/net/ne2000/ne2000_control.inc', 'drivers/net/ne2000/ne2000_data.inc', 'drivers/net/ne2000/ne2000_irq.inc', 'drivers/net/ne2000/ne2000.h', 'drivers/net/ne2000/ne2000_regs.h', 'drivers/net/ne2000/ne2000_ring.h', 'drivers/net/ne2000/ne2000_ring.c', 'arch/x86/x86_flags.h', 'include/types.h', 'include/net_eth.h', 'kernel/irq/tick.h', 'platform/pc98/tick_order.h', 'arch/x86/kernel/idt.h', 'kernel/irq/irq.h', 'include/kprintf.h', 'lib/kstring.h', 'kernel/time/cpu_calibrate.h', 'include/io.h', 'platform/pc98/platform_io.h', 'include/cpu.h', 'arch/x86/arch_cpu.h', 'kernel/idle_work.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_generations.h', 'tools/tests/ne2000_dma_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/ne2k_bh_host.c', 'tools/tests/test_ne2k_bh.py', 'kernel/idle_work.c', 'net/nic.c', 'net/nic.h', 'net/nic_rx.h', 'net/link.h'])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    directory = ROOT / 'drivers/net/ne2000'
    with tempfile.TemporaryDirectory(prefix='ne2k-bh-') as tmp:
        work = Path(tmp)
        originals = {p.name: p.read_text() for p in [directory / 'ne2000.c', *directory.glob('ne2000_*.inc')]}
        # Instrument the real wait entry, before its defensive guard. Any call
        # from IF=0/IRQ is a runtime exit(1), including when hardware never completes.
        originals['ne2000.c'] = originals['ne2000.c'].replace(
            'static int wait_rdc(void)\n{', 'static int wait_rdc(void)\n{\n    bh_wait_check();').replace(
            'static int hw_reset(void)\n{', 'static int hw_reset(void)\n{\n    bh_wait_check();')
        for name, source in originals.items():
            (work / name).write_text(source)
        exe = work / 'host'
        def build():
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-function', '-ffreestanding',
                            '-fno-pie', '-fno-stack-protector', '-ffunction-sections',
                            '-fdata-sections', '-nostdlib', '-static', '-no-pie',
                            '-Wl,--gc-sections', *include_flags(ROOT), '-I' + str(directory),
                            '-DNE2000_SOURCE="' + str(work / 'ne2000.c') + '"',
                            str(ROOT / 'tools/tests/ne2k_bh_host.c'),
                            str(directory / 'ne2000_ring.c'), '-o', str(exe)], check=True)
        build()
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            rc = host32.run([str(exe)], runner=runner).returncode
            assert rc == 0, (runner, rc)
            print(f'GREEN all IF=0 entries, faults, recovery and TX FIFO ({runner})', flush=True)
        if args.mutate:
            for label, old, new in [
                ('leave-service', '        mark_work();\n    }\n    ne2k_imr_sync',
                 '        service(NE2K_IRQ_BUDGET);\n    }\n    ne2k_imr_sync'),
                ('obsolete-in-irq', 'if (!eflags_if() || kctx_irq_depth != 0)',
                 'if (!eflags_if() || nic.in_irq)'),
            ]:
                original = originals['ne2000.c']
                assert original.count(old) == 1, label
                (work / 'ne2000.c').write_text(original.replace(old, new))
                build()
                rc = host32.run([str(exe)], runner='qemu').returncode
                assert rc == 1, (label, rc)
                print(f'MUTATE RED {label}: rc={rc}', flush=True)
                (work / 'ne2000.c').write_text(original)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_ne2k_bh.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    main()
