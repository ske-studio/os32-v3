#!/usr/bin/env python3
"""Product log + ESCC runtime tests; freestanding host32, fake MMIO only."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('data-store', 'escc.c', 'ppc_store8(s->data,(unsigned char)p[i]);', '(void)p[i];'),
    ('drop', 'log.c', 'add_sat(&stats[i].dropped, n);', '(void)n;'),
    ('poll-bound', 'escc.c', 'while (!(rr0 & PM_RR0_TX_READY) && wait++ < PM_POLL_LIMIT);',
     'while (!(rr0 & PM_RR0_TX_READY) && ++wait);'),
    ('sink-order', 'log.c', 'sinks[i]->write(sinks[i]->ctx, bytes, n)',
     'sinks[sink_count-1-i]->write(sinks[sink_count-1-i]->ctx, bytes, n)'),
    ('sink-limit', 'log.c', 'sink_count >= PM_LOG_SINK_MAX', 'sink_count > PM_LOG_SINK_MAX'),
    ('hex', 'log.c', '(28 - 4*j)', '(4*j)'),
]

# Expected exit codes are defined in kboot_log_host.c.
EXACT_MUTATIONS = [
    ('rx-drop', 'escc.c', 'unsigned byte = ppc_load8(s->data);',
     '(void)ppc_load8(s->data); unsigned byte = ppc_load8(s->data);', 201),
    ('rx-try-waits', 'escc.c', 'if (!s || !(ppc_load8(s->csr) & PM_RR0_RX_READY)) return -1;',
     'if (!s) return -1;\n    while (!(ppc_load8(s->csr) & PM_RR0_RX_READY)) {}', 200),
    ('rx-status', 'escc.c', 'if ((status & PM_RR1_OVERRUN) && rx_overruns != (u32)-1)',
     'if ((status & PM_RR1_ALL_SENT) == 0 && rx_overruns != (u32)-1)', 203),
    ('rx-overrun-count', 'escc.c', '++rx_overruns;', 'rx_overruns = 1;', 204),
    ('rx-framing-parity-swap', 'escc.c',
     'if ((status & PM_RR1_FRAMING) && rx_framing_errors != (u32)-1) ++rx_framing_errors;\n'
     '        if ((status & PM_RR1_PARITY) && rx_parity_errors != (u32)-1) ++rx_parity_errors;',
     'if ((status & PM_RR1_PARITY) && rx_framing_errors != (u32)-1) ++rx_framing_errors;\n'
     '        if ((status & PM_RR1_FRAMING) && rx_parity_errors != (u32)-1) ++rx_parity_errors;',
     205),
    ('rx-parity-else-if', 'escc.c', 'if ((status & PM_RR1_PARITY) && rx_parity_errors != (u32)-1)',
     'else if ((status & PM_RR1_PARITY) && rx_parity_errors != (u32)-1)', 207),
    ('clock-pclk', 'log.c', 'ESCC_WR14_BRG_RTXC = 0x00',
     'ESCC_WR14_BRG_RTXC = 0x02', 208),
    ('clock-time-constant', 'log.c', 'ESCC_BRG_RATE / baud - 2',
     'ESCC_BRG_RATE / baud - 1', 209),
    ('fixed-rate-status', 'log.c', 'if (baud) *baud = escc_baud;',
     'if (baud) *baud = 9600;', 210),
]


def exercise(directory, name, filename=None, before=None, after=None):
    for unit in ('log.c', 'escc.c'):
        source = (ROOT / 'platform/powermac' / unit).read_text()
        if unit == filename:
            assert before in source
            assert source.count(before) == 1
            source = source.replace(before, after, 1)
        (directory / unit).write_text(source)
    binary = directory / name
    command = ['gcc', '-m32', '-std=gnu11', '-O2', '-ffreestanding', '-DKBOOT_SH=1',
               '-ffunction-sections', '-Wl,--gc-sections',
               '-fno-builtin', '-fno-pie', '-no-pie', '-fno-stack-protector',
               '-nostdlib', '-static', '-Wall', '-Wextra', '-Werror',
               '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'arch/ppc'),
               '-I' + str(ROOT / 'platform/powermac'),
               '-I' + str(ROOT / 'sdk/include/os32'),
               str(ROOT / 'kernel/port_service.c'),
               str(ROOT / 'tools/ppc/kboot_log_host.c'),
               str(directory / 'log.c'), str(directory / 'escc.c'), '-o', str(binary)]
    subprocess.run(command, check=True, timeout=60)
    result = subprocess.run(['qemu-i386', str(binary)], timeout=10)
    return result.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='kboot-log-', dir=os.environ.get('TMPDIR')) as tmp:
        directory = Path(tmp)
        assert exercise(directory, 'baseline') == 0, 'baseline failed'
        print('baseline GREEN', flush=True)
        if args.mutate:
            for mutation in MUTATIONS:
                rc = exercise(directory, *mutation)
                assert rc > 0, f'{mutation[0]}: expected runtime assertion, got {rc}'
                print(mutation[0] + ' runtime RED', flush=True)
            for name, filename, before, after, expected in EXACT_MUTATIONS:
                rc = exercise(directory, name, filename, before, after)
                assert rc == expected, f'{name}: expected runtime exit {expected}, got {rc}'
                print(f'{name} runtime RED (exit {rc})', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
