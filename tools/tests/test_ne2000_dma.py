"""D04: execute the real NE2000 DMA paths with recorded I/O and PIO seams."""
CHECK = dict(name='ne2000-dma-host', tier=1,
    covers=['drivers/net/ne2000/ne2000.c', 'drivers/net/ne2000/ne2000.h', 'drivers/net/ne2000/ne2000_regs.h', 'drivers/net/ne2000/ne2000_ring.h', 'include/types.h', 'include/net_eth.h', 'kernel/irq/tick.h', 'platform/pc98/tick_order.h', 'arch/x86/kernel/idt.h', 'kernel/irq/irq.h', 'include/kprintf.h', 'lib/kstring.h', 'kernel/time/cpu_calibrate.h', 'include/cpu.h', 'arch/x86/arch_cpu.h', 'tools/tests/ne2000_dma_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=60.0)

import os
from pathlib import Path
import subprocess
import sys
import tempfile

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
    ('success exit', 'if (wait_rdc() == NE2K_OK) return NE2K_OK;', '(void)wait_rdc();'),
]


def build(source, exe):
    subprocess.run([
        'gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-pie', '-fno-stack-protector',
        '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static', '-no-pie',
        '-Wl,--gc-sections', *include_flags(ROOT),
        '-DNE2000_SOURCE="' + str(source) + '"', str(HARNESS), '-o', str(exe),
    ], check=True)


def main():
    with tempfile.TemporaryDirectory(prefix='ne2000-dma-') as td:
        exe = Path(td) / 'host'
        build(SOURCE, exe)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], runner=runner, timeout=30)
            assert result.returncode == 0, (runner, result.returncode)
            print(f'PASS DMA 140 cases: directions/lengths/RDC/retry/abort ({runner})', flush=True)
        if '--mutate' in sys.argv:
            original = SOURCE.read_text()
            for label, old, new in MUTATIONS:
                assert original.count(old) == 1, (label, 'ambiguous anchor')
                source = Path(td) / 'mutant.c'
                source.write_text(original.replace(old, new))
                build(source, exe)  # Compilation failures are never a successful mutation.
                result = host32.run([str(exe)], runner='qemu', timeout=30)
                assert 0 < result.returncode <= 200, (label, result.returncode)
                print(f'MUTATE RED {label}: rc={result.returncode}', flush=True)


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
