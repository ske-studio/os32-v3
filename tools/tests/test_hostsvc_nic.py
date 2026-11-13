#!/usr/bin/env python3
"""A2 real NIC lifetime/selection with fake adapters, link and A3 seams (ILP32)."""
CHECK = dict(name='hostsvc-nic', tier=1, needs=[], budget=3.8, covers=['net/rx_work.c', 'net/rx_work.h', 'include/clock_source.h', 'kernel/irq/tick_budget.h', 'kernel/idle_work.h',
    'net/rx_work.c', 'net/rx_work.h', 'include/clock_source.h', 'include/kernel_tick.h', 'kernel/irq/irq.h', 'kernel/irq/tick_budget.h', 'net/nic.c', 'net/nic.h', 'net/nic_rx.h', 'net/link.h', 'include/net_eth.h',
    'kernel/irq/tick.h', 'platform/pc98/tick_order.h',
    'include/kprintf.h',
    'drivers/net/e82557/e82557_regs.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_fixture.h', 'tools/tests/hostsvc_fixture.c',
    'tools/tests/hostsvc_nic_host.c', 'tools/tests/host32.py',
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
# A2 portion of §11 nic: selection, lifetime, dispatch and TX facade.
# MARK/EOF mutation belongs to the A3 demux stage, not this fake boundary.
MUTATIONS = (
    ('restart-same-peer', 'return 0; /* Same NIC/peer: preserve the handshake and its nonce. */',
     'return OS32_ERR_BUSY;'),
    ('auto-preference', 'choice = adapters[NET_NIC_E82557] ? NET_NIC_E82557 : NET_NIC_LGY98;',
     'choice = adapters[NET_NIC_LGY98] ? NET_NIC_LGY98 : NET_NIC_E82557;'),
    ('release-reference', 'if (adapter) adapter->release();', 'if (0 && adapter) adapter->release();'),
    ('bind-failure-release', 'if (rc) adapter->release();', 'if (0 && rc) adapter->release();'),
    ('tx-result', 'bound ? bound->tx(frame, len) : OS32_ERR_HOSTDOWN',
     'bound ? (bound->tx(frame, len), 0) : OS32_ERR_HOSTDOWN'),
    ('unbound-result', 'int rc = OS32_ERR_HOSTDOWN;', 'int rc = 0;'),
    ('double-bind', 'if (bound) rc = OS32_ERR_BUSY;', 'if (bound) rc = 0;'),
    ('unbind-queues', '    net_rx_reset(NET_RX_LINK);\n    irq_restore(flags);',
     '    net_rx_reset(NET_RX_SFS);\n    irq_restore(flags);'),
    ('fixed-peer', 'count = (int)net_rx_demux_budget(bound, local_mac, fixed_peer ? peer_mac : 0, b);',
     'count = (int)net_rx_demux_budget(bound, local_mac, fixed_peer ? 0 : peer_mac, b);'),
    ('tick-link', '            link_tick();', '            if (0) link_tick();'),
    ('poll-count', 'return count;\n}', 'return count * 2;\n}'),
    ('rx-credit', 'bound ? bound->rx_credit_bytes() : 0',
     'bound ? (bound->rx_credit_bytes(), 0) : 0'),
    ('tick-error-domain', '                rc = OS32_ERR_BUSY;',
     '                rc = TICK_ERR_BUSY;'),
    ('tick-poll-reentry', '!bound || ticking || unbind_due',
     '!bound || unbind_due'),
    ('tick-recursion', 'if (bound && !ticking) {\n        ticking = 1;',
     'if (bound) {\n        ticking = 1;'),
)


def build_run(work, source):
    src = work / 'nic.c'
    src.write_text(source)
    exe = work / 'nic-test'
    subprocess.run(['gcc', *FLAGS, '-I' + str(work), '-I' + str(ROOT),
                    '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT),
                    '-nostdlib', '-static', '-no-pie', str(src), str(ROOT / 'net/rx_work.c'),
                    str(ROOT / 'tools/tests/hostsvc_fixture.c'),
                    str(ROOT / 'tools/tests/hostsvc_nic_host.c'), '-o', str(exe)],
                   check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'net/nic.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-nic-') as tmp:
        work = Path(tmp)
        (work / 'io.h').write_text(
            'unsigned int nic_test_irq_save(void);\n'
            'void nic_test_irq_restore(unsigned int);\n'
            '#define irq_save nic_test_irq_save\n'
            '#define irq_restore nic_test_irq_restore\n'
            'int nic_test_irq_enabled(void);\n#define _irq_enabled nic_test_irq_enabled\n')
        # Production includes are compiled separately; only execution replaces cli.
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))
        subprocess.run([str(cross / 'bin/i386-elf-gcc'), *FLAGS,
                        '-I' + str(ROOT), *include_flags(ROOT),
                        '-c', str(ROOT / 'net/nic.c'), '-o', str(work / 'nic.o')], check=True)
        rc = build_run(work, original)
        if rc:
            print(f'FAIL control runtime rc={rc}', flush=True)
            return 1
        print('GREEN A2 NIC selection/lifetime/TX/poll/IF', flush=True)
        if args.mutate:
            for name, before, after in MUTATIONS:
                if original.count(before) != 1:
                    raise RuntimeError(f'{name}: mutation anchor not unique')
                rc = build_run(work, original.replace(before, after))
                if rc == 0:
                    raise RuntimeError(f'{name}: expected runtime RED, rc={rc}')
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_nic.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
