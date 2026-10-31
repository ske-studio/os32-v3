"""D04: execute real NE2000 DMA, OVW, TX timeout and initialization paths."""
CHECK = dict(name='ne2000-dma-host', tier=4,
    covers=['drivers/net/ne2000/ne2000.c', 'drivers/net/ne2000/ne2000_control.inc', 'drivers/net/ne2000/ne2000_data.inc', 'drivers/net/ne2000/ne2000_irq.inc', 'drivers/net/ne2000/ne2000.h', 'drivers/net/ne2000/ne2000_regs.h', 'drivers/net/ne2000/ne2000_ring.h', 'drivers/net/ne2000/ne2000_ring.c', 'arch/x86/x86_flags.h', 'include/types.h', 'include/net_eth.h', 'kernel/irq/tick.h', 'platform/pc98/tick_order.h', 'arch/x86/kernel/idt.h', 'kernel/irq/irq.h', 'include/kprintf.h', 'lib/kstring.h', 'kernel/time/cpu_calibrate.h', 'include/cpu.h', 'arch/x86/arch_cpu.h', 'tools/tests/ne2000_dma_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/net_m2_test.py', 'tools/net_m4_test.py', 'tools/net_l1_test.py', 'tools/net_l2_test.py'],
    needs=['make:all'], budget=60.0)

import os
from pathlib import Path
import subprocess
import tempfile
import argparse
import ast
import shutil

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'drivers/net/ne2000/ne2000.c'
HARNESS = ROOT / 'tools/tests/ne2000_dma_host.c'
MUTATIONS = [
    ('rounding', '(u16)((len + 1) & ~1)', 'len'),
    ('zero length', 'if (len == 0) return NE2K_OK;', 'if (len == 0) return NE2K_ERR_TIMEOUT;'),
    ('direction', 'write ? NE2K_CR_RD1 : NE2K_CR_RD0', 'write ? NE2K_CR_RD0 : NE2K_CR_RD1'),
    ('PIO direction', 'if (write) ne2k_pio_write', 'if (!write) ne2k_pio_write'),
    ('PIO byte count', 'ne2k_pio_read(nic.cfg.data_port, buf, len);',
     'ne2k_pio_read(nic.cfg.data_port, buf, n);'),
    ('write byte count', 'ne2k_pio_write(nic.cfg.data_port, buf, len);',
     'ne2k_pio_write(nic.cfg.data_port, buf, n);'),
    ('address', 'wr(NE2K_P0_RSAR1, (u8)(addr >> 8));', 'wr(NE2K_P0_RSAR1, (u8)addr);'),
    ('stale RDC', 'wr(NE2K_P0_ISR, NE2K_ISR_RDC);      /* 古い RDC を消す */', ''),
    ('retry limit', 'try_i < NE2K_RDMA_ATTEMPTS', 'try_i < 1'),
    ('retry count', 'nic.st.rdc_retry++;', 'nic.st.rdc_retry += 2;'),
    ('timeout count', 'nic.st.rdc_timeout++;', 'nic.st.rdc_timeout += 2;'),
    ('deadline', 'waited >= NE2K_RDC_WAIT_US', 'waited >= NE2K_RDC_WAIT_US - NE2K_RDC_POLL_US'),
    ('abort', 'cr_page0_idle();                    /* RD2 = abort */', ''),
    ('success ACK', 'wr(NE2K_P0_ISR, NE2K_ISR_RDC);\n            return NE2K_OK;',
     'return NE2K_OK;'),
    ('retry only on reissue', 'if (try_i > 0) nic.st.rdc_retry++;',
     'nic.st.rdc_retry++;'),
    ('OVW backlog', 'nic.rx_backlog = (u8)(r == 1);\n    /* 旧送信',
     '/* 旧送信'),
    ('OVW completion', '        tx_complete(isr);\n        resend = 0;',
     '        resend = 0;'),
    ('reinit count', '    nic.st.reinit++;\n    rc = bring_up();',
     '    nic.st.reinit += 2;\n    rc = bring_up();'),
    ('OVW tick wait', '(u32)(tick_count - nic.ovw_tick) < NE2K_OVW_WAIT_TICKS',
     '(u32)(tick_count - nic.ovw_tick) < 1'),
    ('OVW IF0 delay', 'cpu_delay_us(NE2K_OVW_WAIT_US);', ''),
    ('OVW resend', 'resend = nic.ovw_txp_was && !nic.ovw_tx_done;', 'resend = 0;'),
    ('send padding', 'kmemset(nic.tx_buf + padded, 0, NE2K_ETH_MIN_LEN - padded);', ''),
    ('init clear', 'kmemset(&nic, 0, sizeof(nic));', ''),
    ('success exit', 'if (wait_rdc() == NE2K_OK) return NE2K_OK;', '(void)wait_rdc();'),
]


GROUPS = {'all': 0, 'dma': 1, 'ovw': 2, 'reinit': 3, 'redundancy': 4}


def layout_flags():
    offsets = {}
    for name in ('net_m2_test.py', 'net_m4_test.py', 'net_l1_test.py', 'net_l2_test.py'):
        for node in ast.parse((ROOT / 'tools' / name).read_text()).body:
            if isinstance(node, ast.Assign) and len(node.targets) == 1:
                target = node.targets[0]
                if isinstance(target, ast.Name) and target.id in (
                        'NIC_STATE', 'NIC_IRQ_ON', 'NIC_RXQ_COUNT', 'NIC_ST'):
                    value = ast.literal_eval(node.value)
                    assert offsets.setdefault(target.id, value) == value, (name, target.id)
    return [f'-DHOST_{name}={value}' for name, value in offsets.items()]


def build(source, exe, group=0):
    subprocess.run([
        'gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-pie', '-fno-stack-protector',
        '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static', '-no-pie',
        '-Wl,--gc-sections', *layout_flags(), *include_flags(ROOT), '-I' + str(SOURCE.parent),
        f'-DTEST_GROUP={group}', '-DNE2000_SOURCE="' + str(source) + '"', str(HARNESS), str(SOURCE.parent / 'ne2000_ring.c'), '-o', str(exe),
    ], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--case', choices=GROUPS, default='all')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ne2000-dma-') as td:
        exe = Path(td) / 'host'
        build(SOURCE, exe, GROUPS[args.case])
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], runner=runner, timeout=30)
            assert result.returncode == 0, (runner, result.returncode)
            print(f'PASS {args.case}: DMA/OVW/TX timeout/IF waits/send lengths/init ({runner})', flush=True)
        if args.mutate:
            paths = [SOURCE, *sorted(SOURCE.parent.glob('ne2000_*.inc'))]
            originals = {path.name: path.read_text() for path in paths}
            for path in paths:
                shutil.copyfile(path, Path(td) / path.name)
            for label, old, new in MUTATIONS:
                matches = [name for name, original in originals.items() if old in original]
                assert len(matches) == 1, (label, 'ambiguous file')
                name = matches[0]
                original = originals[name]
                assert original.count(old) == 1, (label, 'ambiguous anchor')
                changed = Path(td) / name
                changed.write_text(original.replace(old, new))
                build(Path(td) / SOURCE.name, exe)  # Compilation failures are never a successful mutation.
                result = host32.run([str(exe)], runner='qemu', timeout=30)
                assert 0 < result.returncode <= 200, (label, result.returncode)
                print(f'MUTATE RED {label}: rc={result.returncode}', flush=True)
                changed.write_text(original)


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_ne2000_dma.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
