#!/usr/bin/env python3
"""A10 LAN transport with real shared RX queues, libc-free ILP32 runtime."""
CHECK = dict(name='hostsvc-sfs-lan', tier=1, needs=[], budget=3.6, covers=['tools/tests/rx_io_clock.h', 'tools/tests/idle_io_sources.py',
    'kernel/io_work.[ch]', 'sys/kern/io_clock.h', 'kernel/io_policy.[ch]', 'sys/kern/idle_work.h', 'sys/kern/exec/appslot.h', 'sys/net/rx_work.c', 'sys/net/rx_work.h', 'sys/hal/time/clock_source.h', 'sys/kern/irq/tick_budget.h',
    'sys/fs/serialfs/sfs_lan.c', 'sys/fs/serialfs/sfs_lan.h',
    'sys/fs/serialfs/sfs_client.h', 'sys/fs/serialfs/sfs_proto.c', 'sys/fs/serialfs/sfs_proto.h',
    'sys/net/nic_rx.c', 'sys/net/nic.h', 'sys/net/nic_rx.h', 'sys/net/link.h',
    'lib/crc32.c', 'lib/crc32.h', 'lib/le_internal.h', 'sys/kern/kstring.h',
    'sys/include/net_eth.h', 'sys/include/types.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_sfs_lan_host.c', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk'],
    basis=['docs/design/net/nic.md 受信の振り分け', 'docs/design/fs/serialfs/serialfs_persistent.md 期限', 'docs/design/fs/serialfs/serialfs_persistent.md 何をするか'],
)

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags
from idle_io_sources import idle_io_sources

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
MUTATIONS = (
    ('absolute-tx', 'if ((i32)(deadline - limit) < 0) limit = deadline;',
     '(void)deadline;'),
    ('local-tx', 'limit = lan_now(ctx) + sfs_ms_ticks(SFS_LAN_PUT_MS);',
     'limit = lan_now(ctx) + 200;'),
    ('expired-tx', 'if ((i32)(lan_now(ctx) - limit) >= 0) return OS32_ERR_TIMEOUT;\n        r =',
     'if ((i32)(lan_now(ctx) - limit) > 0) return OS32_ERR_TIMEOUT;\n        r ='),
    ('rx-other-queue', 'net_rx_take(NET_RX_SFS, lan->rx, sizeof(lan->rx), &n)',
     'net_rx_take(NET_RX_LINK, lan->rx, sizeof(lan->rx), &n)'),
    ('reset-other-queue', 'net_rx_reset(NET_RX_SFS);', 'net_rx_reset(NET_RX_LINK);'),
    ('private-tx', 'net_tx(lan->tx, size)', 'lan->nic->send(lan->tx, size)'),
    ('private-mac', '(void)net_mac(lan->mac);', 'nic->mac(lan->mac);'),
)


def build_run(work, source):
    src = work / 'sfs_lan.c'
    src.write_text(source)
    exe = work / 'sfs-lan-test'
    subprocess.run(['gcc', *FLAGS, '-I' + str(work), '-I' + str(ROOT),
                    *include_flags(ROOT), '-nostdlib', '-static', '-no-pie',
                    str(src), str(ROOT / 'sys/net/nic_rx.c'), str(ROOT / 'sys/net/rx_work.c'),
                    str(ROOT / 'sys/fs/serialfs/sfs_proto.c'), str(ROOT / 'lib/crc32.c'),
                    str(ROOT / 'tools/tests/hostsvc_sfs_lan_host.c'), *idle_io_sources(ROOT, work, compat=False), '-o', str(exe)],
                   check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'sys/fs/serialfs/sfs_lan.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-sfs-lan-') as tmp:
        work = Path(tmp)
        (work / 'io.h').write_text(
            'unsigned int nic_test_irq_save(void);\n'
            'void nic_test_irq_restore(unsigned int);\n'
            '#define irq_save nic_test_irq_save\n'
            '#define irq_restore nic_test_irq_restore\n')
        rc = build_run(work, original)
        if rc:
            raise RuntimeError(f'control failed at runtime: rc={rc}')
        print('GREEN A10 shared RX/TX/MAC, reset isolation, CRC/peer, TX wait', flush=True)
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
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_sfs_lan.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
