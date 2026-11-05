#!/usr/bin/env python3
"""A3 real receive demux/queues with fake NIC polling (ILP32)."""
CHECK = dict(name='hostsvc-nic-rx', tier=1, needs=[], budget=3, covers=[
    'net/nic_rx.c', 'lib/kstring.h', 'net/nic.h', 'net/nic_rx.h', 'net/link.h', 'include/net_eth.h',
    'kernel/irq/tick.h', 'platform/pc98/tick_order.h',
    'drivers/net/e82557/e82557_regs.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_fixture.h', 'tools/tests/hostsvc_fixture.c',
    'tools/tests/hostsvc_nic_rx_host.c', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk'])

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
# Design §11 nic: runtime discrimination, queue independence and overflow.
MUTATIONS = (
    ('mark-byte0', 'n >= 3 && p[0] == 0x05 && p[1] == 0x53 && p[2] == 0x46', 'n >= 1 && p[0] == 0x05'),
    ('stop-on-full', '            continue;\n        }\n        u32 tail', '            break;\n        }\n        u32 tail'),
    ('drop-count', 'net_rx_sfs_dropped++;', 'net_rx_sfs_dropped += 2;'),
    ('link-drop-count', 'net_rx_link_dropped++;', 'net_rx_link_dropped += 2;'),
    ('sender', 'peer && !mac_equal(rx_frame + ETH_ALEN, peer)', '0 && peer && !mac_equal(rx_frame + ETH_ALEN, peer)'),
    ('broadcast', 'if (!own && (sfs || n == 0 || p[0] != LINK_OP_HELLO))', 'if (0 && !own && (sfs || n == 0 || p[0] != LINK_OP_HELLO))'),
    ('consume-nospace', 'if (cap >= *len)', 'if (cap + 1 >= *len)'),
    ('scan-budget', 'scan < NET_RX_SCAN_MAX', 'scan <= NET_RX_SCAN_MAX'),
    ('reset-other', 'if (q) q->head = q->count = 0;', 'if (q) { sfs_queue.count = link_queue.count = 0; }'),
    ('restore-if', '    irq_restore(flags);\n    return rc;', '    irq_restore(flags | 1);\n    return rc;'),
)


def build_run(work, source):
    src = work / 'nic_rx.c'
    src.write_text(source)
    exe = work / 'nic-test'
    subprocess.run(['gcc', *FLAGS, '-I' + str(work), '-I' + str(ROOT),
                    '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT),
                    '-nostdlib', '-static', '-no-pie', str(src),
                    str(ROOT / 'tools/tests/hostsvc_fixture.c'),
                    str(ROOT / 'tools/tests/hostsvc_nic_rx_host.c'), '-o', str(exe)],
                   check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'net/nic_rx.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-nic-rx-') as tmp:
        work = Path(tmp)
        (work / 'io.h').write_text(
            'unsigned int nic_test_irq_save(void);\n'
            'void nic_test_irq_restore(unsigned int);\n'
            '#define irq_save nic_test_irq_save\n'
            '#define irq_restore nic_test_irq_restore\n')
        # Production includes are compiled separately; only execution replaces cli.
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))
        subprocess.run([str(cross / 'bin/i386-elf-gcc'), *FLAGS,
                        '-I' + str(ROOT), *include_flags(ROOT),
                        '-c', str(ROOT / 'net/nic_rx.c'), '-o', str(work / 'nic.o')], check=True)
        rc = build_run(work, original)
        if rc:
            print(f'FAIL control runtime rc={rc}', flush=True)
            return 1
        print('GREEN A3 MARK/EOF/queues/overflow/peer/poll/IF', flush=True)
        if args.mutate:
            for name, before, after in MUTATIONS:
                if original.count(before) != 1:
                    raise RuntimeError(f'{name}: mutation anchor not unique')
                rc = build_run(work, original.replace(before, after))
                if rc <= 0:
                    raise RuntimeError(f'{name}: expected runtime RED, rc={rc}')
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_nic_rx.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
