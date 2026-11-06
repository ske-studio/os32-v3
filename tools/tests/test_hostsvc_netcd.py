#!/usr/bin/env python3
"""D3 network CD with real persistent session, block RPC and wire protocol."""
CHECK = dict(name='hostsvc-netcd', tier=1, needs=[], budget=120, covers=[
    'drivers/block/netcd.c', 'drivers/block/netcd.h', 'drivers/base/dev.h',
    'fs/serialfs/sfs_block.c', 'fs/serialfs/sfs_block.h',
    'fs/serialfs/serialfs_persistent.c', 'fs/serialfs/serialfs.h',
    'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_client.h',
    'fs/serialfs/sfs_proto.c', 'fs/serialfs/sfs_proto.h',
    'net/host_mailbox.h', 'net/nic.h', 'fs/vfs.h',
    'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_netcd_host.c', 'tools/tests/hostsvc_fixture.c',
    'tools/tests/hostsvc_fixture.h', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk', 'include/types.h',
    'lib/crc32.c', 'lib/crc32.h', 'lib/le_internal.h',
    'lib/kstring.h', 'lib/kstring_asm.asm'])

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('size-lo', 'media->size[0] == block->size[0]', '1'),
    ('size-hi', 'media->size[1] == block->size[1]', '1'),
    ('mtime-lo', 'media->mtime[0] == block->mtime[0]', '1'),
    ('mtime-hi', 'media->mtime[1] == block->mtime[1]', '1'),
    ('generation', 'media->media_gen++;', 'media->media_gen += 0;'),
    ('snapshot', 'media->alive_seen = alive.observation;',
     'media->alive_seen = alive.observation + 1;'),
    ('alive', '(alive.valid && alive.observation != media->alive_seen)', '0'),
    ('idle', '>= NETCD_IDLE_TICKS', '> NETCD_IDLE_TICKS'),
    ('sid', 'if (client->sid != ctx->block.sid) refresh = 1;', ''),
    ('close', 'rc = sfs_bclose(&ctx->block, deadline);', 'rc = 0;'),
    ('range', '(u32)count > dev->total_sects - lba', '(u32)count > dev->total_sects'),
    ('sector-hi', '(opened.size[1] << (32 - NETCD_SECTOR_SHIFT))', '0'),
    ('chunk', 'count > SFS_BREAD_COUNT_MAX ? SFS_BREAD_COUNT_MAX : count', 'count'),
    ('deadline', 'sfs_bread(&ctx->block, lba, chunk, dst, bytes, deadline)',
     'sfs_bread(&ctx->block, lba, chunk, dst, bytes, deadline + 1)'),
    ('restart', '!client->sid || client->sid != ctx->block.sid',
     'client->sid && client->sid != ctx->block.sid'),
    ('stale', 'if (rc == OS32_ERR_STALE) ctx->recheck = 1;', ''),
)


def build_run(work, source):
    src = work / 'netcd.c'
    src.write_text(source)
    exe = work / 'netcd-test'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
        '-I' + str(ROOT), '-I' + str(ROOT / 'drivers/block'), *include_flags(ROOT),
        '-DPRODUCT_SOURCE="' + str(src) + '"',
        *[str(ROOT / p) for p in [
            'tools/tests/hostsvc_netcd_host.c', 'tools/tests/hostsvc_fixture.c',
            'fs/serialfs/serialfs_persistent.c', 'fs/serialfs/sfs_block.c',
            'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_proto.c', 'lib/crc32.c']],
        str(work / 'kstring.o'), '-o', str(exe)], check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT, timeout=120).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'drivers/block/netcd.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-netcd-') as tmp:
        work = Path(tmp)
        subprocess.run(['nasm', '-f', 'elf32', str(ROOT / 'lib/kstring_asm.asm'),
                        '-o', str(work / 'kstring.o')], check=True)
        rc = build_run(work, source)
        if rc:
            raise RuntimeError(f'control failed at runtime: rc={rc}')
        print('GREEN D3 image/2048B/100MiB/seq/ALIVE/sid/u32/range/deadline', flush=True)
        if args.mutate:
            for name, before, after in MUTATIONS:
                if source.count(before) != 1:
                    raise RuntimeError(f'{name}: mutation anchor not unique')
                rc = build_run(work, source.replace(before, after))
                if rc <= 0:
                    raise RuntimeError(f'{name}: expected runtime RED, rc={rc}')
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_netcd.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
