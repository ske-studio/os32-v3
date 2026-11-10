#!/usr/bin/env python3
"""Product log + ESCC runtime tests; freestanding host32, fake MMIO only."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-kboot-log', budget=4, needs=[], covers=['platform/powermac/log.c', 'platform/powermac/kboot_log.h',
                     'platform/powermac/escc.c', 'platform/powermac/ofw.h',
                     'platform/powermac/ppc_layout.h', 'arch/ppc/ppc_boot.h',
                     'include/types.h', 'include/callconv.h', 'tools/ppc/kboot_log_host.c',
                     'tools/ppc/kboot_log.py'])
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


def exercise(directory, name, filename=None, before=None, after=None):
    for unit in ('log.c', 'escc.c'):
        source = (ROOT / 'platform/powermac' / unit).read_text()
        if unit == filename:
            assert before in source
            assert source.count(before) == 1
            source = source.replace(before, after, 1)
        (directory / unit).write_text(source)
    binary = directory / name
    command = ['gcc', '-m32', '-std=gnu11', '-O2', '-ffreestanding',
               '-fno-builtin', '-fno-pie', '-no-pie', '-fno-stack-protector',
               '-nostdlib', '-static', '-Wall', '-Wextra', '-Werror',
               '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'arch/ppc'),
               '-I' + str(ROOT / 'platform/powermac'),
               str(ROOT / 'tools/ppc/kboot_log_host.c'),
               str(directory / 'log.c'), str(directory / 'escc.c'), '-o', str(binary)]
    subprocess.run(command, check=True, timeout=60)
    result = subprocess.run(['qemu-i386', str(binary)], timeout=10)
    return result.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='kboot-log-', dir=os.environ['TMPDIR']) as tmp:
        directory = Path(tmp)
        assert exercise(directory, 'baseline') == 0, 'baseline failed'
        print('baseline GREEN', flush=True)
        if args.mutate:
            for mutation in MUTATIONS:
                assert exercise(directory, *mutation) != 0, mutation[0] + ' survived'
                print(mutation[0] + ' runtime RED', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
