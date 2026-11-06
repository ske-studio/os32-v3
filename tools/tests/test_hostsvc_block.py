#!/usr/bin/env python3
"""D2 stateless block RPC, libc-free ILP32 runtime."""
CHECK = dict(name='hostsvc-block', tier=1, needs=[], budget=5, covers=[
    'fs/serialfs/sfs_block.c', 'fs/serialfs/sfs_block.h',
    'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_client.h',
    'fs/serialfs/serialfs.h', 'fs/serialfs/sfs_proto.c', 'fs/serialfs/sfs_proto.h',
    'fs/vfs.h', 'include/types.h', 'include/time_units.h',
    'net/nic.h', 'net/link.h', 'drivers/net/ne2000/ne2000.h',
    'lib/crc32.c', 'lib/crc32.h', 'lib/le_internal.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_block_host.c', 'tools/tests/hostsvc_fixture.h',
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
    ('sid', 'if (sid && (*out)->sid != sid)', 'if (0 && sid && (*out)->sid != sid)'),
    ('size-align', '(opened.size[0] & (SFS_LOGICAL_BLOCK_SIZE - 1))', '0'),
    ('size-limit', 'opened.size[1] >= SFS_LOGICAL_BLOCK_SIZE', 'opened.size[1] > SFS_LOGICAL_BLOCK_SIZE'),
    ('mtime-hi', 'sfs_get32(data + SFS_BOPEN_MTIME_HI_OFF)', '0'),
    ('size-hi', 'sfs_get32(data + SFS_BOPEN_SIZE_HI_OFF)', '0'),
    ('range', 'count > sectors - lba', 'count > sectors'),
    ('short-read', 'if (reply.length != SFS_BREAD_DATA_OFF + bytes)', 'if (0 && reply.length != SFS_BREAD_DATA_OFF + bytes)'),
    ('copy', 'data[SFS_BREAD_DATA_OFF + i]', '0'),
    ('stale', 'if (rc == OS32_ERR_STALE) sfs_persistent_stale();', ''),
    ('deadline', 'in, len, reply, deadline);', 'in, len, reply, deadline + 1);'),
)



def build_run(work, source):
    src = work / 'sfs_block.c'
    src.write_text(source)
    exe = work / 'sfs-deadline-test'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT),
                    *include_flags(ROOT), '-nostdlib', '-static', '-no-pie',
                    str(src), str(ROOT / 'fs/serialfs/sfs_client.c'),
                    str(ROOT / 'tools/tests/hostsvc_fixture.c'),
                    str(ROOT / 'fs/serialfs/sfs_proto.c'), str(ROOT / 'lib/crc32.c'),
                    str(ROOT / 'tools/tests/hostsvc_block_host.c'), '-o', str(exe)],
                   check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'fs/serialfs/sfs_block.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-block-') as tmp:
        work = Path(tmp)
        rc = build_run(work, original)
        if rc:
            raise RuntimeError(f'control failed at runtime: rc={rc}')
        print('GREEN D2 block wire/identity/range/sid/short-read/shared deadline', flush=True)
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
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_block.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
