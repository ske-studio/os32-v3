#!/usr/bin/env python3
"""C3 absolute deadlines and caller-owned replies, libc-free ILP32 runtime."""
CHECK = dict(name='hostsvc-sfs-deadline', tier=1, needs=[], budget=5, covers=[
    'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_client.h',
    'fs/serialfs/serialfs.h', 'fs/serialfs/sfs_proto.c', 'fs/serialfs/sfs_proto.h',
    'fs/vfs.h', 'include/types.h', 'include/time_units.h',
    'net/nic.h', 'net/link.h', 'drivers/net/ne2000/ne2000.h',
    'lib/crc32.c', 'lib/crc32.h', 'lib/le_internal.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_sfs_deadline_host.c', 'tools/tests/hostsvc_fixture.h',
    'tools/tests/hostsvc_fixture.c', 'tools/tests/host32.py',
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
MUTATIONS = (
    ('bounded-send', '(void)c->io->put_until(c->ctx, c->tx, flen, deadline);',
     '(void)c->io->put(c->ctx, c->tx, flen);'),
    ('bounded-oneway', 'c->io->put_until(c->ctx, c->tx, flen, deadline) :',
     'c->io->put(c->ctx, c->tx, flen) :'),
    ('oneway-expired', 'if ((i32)(c->io->now(c->ctx) - deadline) >= 0) return OS32_ERR_TIMEOUT;', ''),
    ('deadline', 'if ((i32)(now - deadline) >= 0) {', 'if ((i32)(now - deadline) > 0) {'),
    ('retry', '#define SFS_LAN_RETRY_TICKS 10u', '#define SFS_LAN_RETRY_TICKS 11u'),
    ('retry-after-tx', 'retry = c->io->now(c->ctx) + SFS_LAN_RETRY_TICKS;',
     'retry = now + SFS_LAN_RETRY_TICKS;'),
    ('seq-retry', 'if (sent) c->resends++;', 'if (sent) { c->resends++; c->seq++; }'),
    ('sticky-dead', 'if (rc) return rc;\n    if (f.type == SFS_T_ERR)',
     'if (rc) { c->dead = SFS_DEAD_FAILS; return rc; }\n    if (f.type == SFS_T_ERR)'),
    ('stale', '        sfs_persistent_stale();', ''),
    ('no-recovery', 'if (attempt == 1) return rc;', 'if (attempt == 0) return rc;'),
    ('two-recoveries', 'if (attempt == 1) return rc;', 'if (attempt == 2) return rc;'),
    ('copy', '((u8 *)out->data)[i] = f.payload[i];', '((u8 *)out->data)[i] = 0;'),
    ('capacity', 'if (out->capacity < f.len) return OS32_ERR_NOSPC;', ''),
    ('shared-deadline', 'out, deadline);\n        if (rc != OS32_ERR_STALE)',
     'out, deadline + 100);\n        if (rc != OS32_ERR_STALE)'),
)


def build_run(work, source):
    src = work / 'sfs_client.c'
    src.write_text(source)
    exe = work / 'sfs-deadline-test'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT),
                    *include_flags(ROOT), '-nostdlib', '-static', '-no-pie',
                    str(src), str(ROOT / 'tools/tests/hostsvc_fixture.c'),
                    str(ROOT / 'fs/serialfs/sfs_proto.c'), str(ROOT / 'lib/crc32.c'),
                    str(ROOT / 'tools/tests/hostsvc_sfs_deadline_host.c'), '-o', str(exe)],
                   check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'fs/serialfs/sfs_client.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-sfs-deadline-') as tmp:
        work = Path(tmp)
        rc = build_run(work, original)
        if rc:
            raise RuntimeError(f'control failed at runtime: rc={rc}')
        print('GREEN C3 absolute deadline, wrap, retry, reply ownership, stale, HELLO, UART separation', flush=True)
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
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_sfs_deadline.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
