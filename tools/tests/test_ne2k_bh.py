"""A2-t2: deferred NE2K device work across every disabled-IF entry."""
import argparse
from idle_io_sources import idle_io_sources
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ne2k-bh-host', tier=4, needs=[], budget=4,
    covers=['tools/tests/rx_io_clock.h', 'tools/tests/idle_io_sources.py', 'kernel/io_work.[ch]',
            'kernel/io_clock.h', 'kernel/io_policy.[ch]', 'exec/appslot.h',
            'kernel/time/tick_clock.h', 'net/nic.h', 'net/rx_work.h', 'net/rx_work.c', 'include/clock_source.h', 'include/kernel_tick.h', 'kernel/irq/tick_budget.h', 'drivers/net/ne2000/ne2000.c', 'drivers/net/ne2000/ne2000_control.inc', 'drivers/net/ne2000/ne2000_data.inc', 'drivers/net/ne2000/ne2000_irq.inc', 'drivers/net/ne2000/ne2000.h', 'drivers/net/ne2000/ne2000_regs.h', 'drivers/net/ne2000/ne2000_ring.h', 'drivers/net/ne2000/ne2000_ring.c', 'arch/x86/x86_flags.h', 'include/types.h', 'include/net_eth.h', 'kernel/irq/tick.h', 'platform/pc98/tick_order.h', 'arch/x86/kernel/idt.h', 'kernel/irq/irq.h', 'include/kprintf.h', 'lib/kstring.h', 'kernel/time/cpu_calibrate.h', 'include/io.h', 'platform/pc98/platform_io.h', 'include/cpu.h', 'arch/x86/arch_cpu.h', 'kernel/idle_work.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_generations.h', 'tools/tests/ne2000_dma_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/ne2k_bh_host.c', 'tools/tests/test_ne2k_bh.py', 'kernel/idle_work.c', 'net/link_platform.h', 'platform/pc98/platform_link_inline.h', 'net/nic.c', 'net/nic_rx.h', 'net/link.h'],
    basis=['TICK_1KHZ_NEXT_DESIGN §2-4', 'IO_TIER_DESIGN §2'],
)


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
                            str(directory / 'ne2000_ring.c'), *idle_io_sources(ROOT, work), '-o', str(exe)], check=True)
        build()
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            rc = host32.run([str(exe)], runner=runner).returncode
            assert rc == 0, (runner, rc)
            print(f'GREEN all IF=0 entries, faults, recovery and TX FIFO ({runner})', flush=True)
        if args.mutate:
            for label, filename, old, new in [
                ('completion-time', 'ne2000_data.inc', 'nic.tx_busy && io_slice_take(slice)', 'nic.tx_busy'),
                ('ovw-time', 'ne2000_data.inc', 'if (!io_slice_take(slice)) return 1;', 'if ((void)slice, 0) return 1;'),
                ('descriptor-policy', 'ne2000.c', '.budget_min_us = 1, .budget_max_us = IO_PASS_US,', '.budget_min_us = 1, .budget_max_us = IO_PASS_US + 1,'),
                ('time-limit', 'ne2000.c', 'io_slice_limit(slice, NET_RX_WORK_LIMIT_US, ~0u)', 'io_slice_limit(slice, IO_PASS_US, ~0u)'),
                ('rx-time', 'ne2000_data.inc', '!io_slice_take(slice)) return 3;', '((void)slice, 0)) return 3;'),
                ('tx-time', 'ne2000.c', '!nic.tx_busy && nic.txq_count && io_slice_take(slice)', '!nic.tx_busy && nic.txq_count'),
                ('recovery-time', 'ne2000_data.inc', 'if (!io_slice_take(slice)) return;', 'if ((void)slice, 0) return;'),
                ('ovw-yield', 'ne2000_data.inc', 'if (r == 3) {', 'if (0) {'),
                ('active-more', 'ne2000.c', '? IO_MORE : IO_DONE;', '? IO_DONE : IO_DONE;'),
                ('request-mark_work', 'ne2000.c', 'io_request(IO_SVC_NE2K_BH); /* mark_work */', '(void)0;'),
                ('request-wait guard', 'ne2000.c', 'io_request(IO_SVC_NE2K_BH); /* wait guard */', '(void)0;'),
                ('request-init', 'ne2000_control.inc', 'io_request(IO_SVC_NE2K_BH); /* init */', '(void)0;'),
                ('request-IRQ busy', 'ne2000_irq.inc', 'io_request(IO_SVC_NE2K_BH); /* IRQ busy */', '(void)0;'),
                ('request-poll busy', 'ne2000_irq.inc', 'io_request(IO_SVC_NE2K_BH); /* poll busy */', '(void)0;'),
                ('request-IRQ', 'ne2000_irq.inc', 'io_request(IO_SVC_NE2K_BH); /* IRQ */', '(void)0;'),
                ('request-timer', 'ne2000_irq.inc', 'io_request(IO_SVC_NE2K_BH); /* timer */', '(void)0;'),
                ('request-send', 'ne2000_data.inc', 'io_request(IO_SVC_NE2K_BH); /* send */', '(void)0;'),
                ('request-recovery', 'ne2000_data.inc', 'io_request(IO_SVC_NE2K_BH); /* recovery */', '(void)0;'),
                ('request-TX DMA failure', 'ne2000_data.inc', 'io_request(IO_SVC_NE2K_BH); /* TX DMA failure */', '(void)0;'),
                ('request-RX backlog', 'ne2000_data.inc', 'io_request(IO_SVC_NE2K_BH); /* RX backlog */', '(void)0;'),
                ('request-RX space', 'ne2000_data.inc', 'io_request(IO_SVC_NE2K_BH); /* RX space */', '(void)0;'),
                ('request-OVW start', 'ne2000_data.inc', 'io_request(IO_SVC_NE2K_BH); /* OVW start */', '(void)0;'),
                ('request-OVW continuation', 'ne2000_data.inc', 'io_request(IO_SVC_NE2K_BH); /* OVW continuation */', '(void)0;'),
                ('enqueue-service', 'ne2000_data.inc', '        net_rx_service();', '        (void)0;'),
                ('batch-share', 'ne2000.c', '    if (!net_rx_batch_begin_slice(slice)) return IO_MORE;', '    (void)0;'),
                ('drop-full', 'ne2000_data.inc', '    if (nic.rxq_count >= NE2K_RXQ_SLOTS) return 2;', '    if (nic.rxq_count >= NE2K_RXQ_SLOTS) { nic.st.rx_dropped++; return 1; }'),
                ('full-after-dma', 'ne2000_data.inc',
                 '    if (nic.rxq_count >= NE2K_RXQ_SLOTS) return 2;\n\n    if (dma_read((u16)nic.rx_next << 8, raw, NE2K_RX_HDR_LEN)) return -1;',
                 '    if (dma_read((u16)nic.rx_next << 8, raw, NE2K_RX_HDR_LEN)) return -1;\n    if (nic.rxq_count >= NE2K_RXQ_SLOTS) return 2;'),
                ('count-blocked', 'ne2000.c', '            if (blocked) break;', '            (void)blocked;'),
                ('leave-service', 'ne2000.c', '        mark_work();\n    }\n    /* CNT may remain',
                 '        { struct io_slice bad = {0}; io_slice_begin(&bad, 120, ~0u); service(NE2K_IRQ_BUDGET, &bad); }\n    }\n    /* CNT may remain'),
                ('backlog-mask', 'ne2000.c', '!nic.need_reinit)', '!nic.rx_backlog && !nic.need_reinit)'),
                ('cnt-mask', 'ne2000.c', 'NE2K_ISR_CNT | NE2K_ISR_OVW', 'NE2K_ISR_OVW'),
                ('one-round', 'ne2000.c', 'round < NE2K_BH_ROUNDS', 'round < 1'),
                ('obsolete-in-irq', 'ne2000.c', 'if (!eflags_if() || kctx_irq_depth != 0)',
                 'if (!eflags_if() || nic.in_irq)'),
            ]:
                original = originals[filename]
                assert original.count(old) == 1, label
                (work / filename).write_text(original.replace(old, new))
                build()
                rc = host32.run([str(exe)], runner='qemu').returncode
                assert rc == 1, (label, rc)
                print(f'MUTATE RED {label}: rc={rc}', flush=True)
                (work / filename).write_text(original)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_ne2k_bh.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    main()
