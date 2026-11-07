#!/usr/bin/env python3
"""Reconnect during open: real VFS mount state, FD table and SerialFS client."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

CHECK = dict(name='hostsvc-open', tier=1, needs=[], budget=10, covers=[
    'fs/serialfs/serialfs_persistent.c', 'fs/serialfs/serialfs.h',
    'fs/serialfs/serialfs.c', 'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_client.h',
    'fs/serialfs/sfs_proto.c', 'fs/serialfs/sfs_proto.h',
    'fs/vfs.c', 'fs/vfs.h', 'fs/vfs_device.h',
    'net/host_mailbox.c', 'net/host_mailbox.h', 'net/nic.h',
    'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_fixture.c', 'tools/tests/hostsvc_fixture.h',
    'tools/tests/hostsvc_open_host.c', 'fs/vfs_fd.c',
    'fs/vfs_fd_stdin.inc', 'fs/vfs_stdin.h', 'fs/fd_redirect.h', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk',
    'include/types.h', 'include/io.h', 'mm/kmalloc.h',
    'lib/crc32.c', 'lib/crc32.h', 'lib/le_internal.h', 'lib/kstring.h',
    'lib/kstring_asm.asm', 'include/console.h', 'include/memmap.h'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('no-retry', 'goto open_probe;', 'if (0) { goto open_probe; } return VFS_ERR_STALE;'),
    ('extra-retry', 'if (retried) return VFS_ERR_STALE;',
     'if (retried > 1) return VFS_ERR_STALE;'),
    ('mount-refresh', 'vfs_fd_stale_mount(fs_ctx);', '(void)fs_ctx;'),
)


def build_run(work, source):
    src = work / 'vfs_fd.c'
    src.write_text(source)
    exe = work / 'open-test'
    subprocess.run(['nasm', '-f', 'elf32', str(ROOT / 'lib/kstring_asm.asm'),
                    '-o', str(work / 'kstring.o')], check=True)
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O0',
        '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-ffreestanding',
        '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
        '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
        '-I' + str(ROOT), '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT),
        '-DPRODUCT_SOURCE="' + str(src) + '"',
        *[str(ROOT / p) for p in [
            'tools/tests/hostsvc_open_host.c', 'tools/tests/hostsvc_fixture.c',
            'fs/vfs.c', 'fs/serialfs/sfs_client.c', 'fs/serialfs/sfs_proto.c',
            'fs/serialfs/serialfs.c', 'lib/crc32.c']],
        str(work / 'kstring.o'), '-o', str(exe)], check=True)
    return host32.run([str(exe)], timeout=30).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'fs/vfs_fd.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-open-') as tmp:
        work = Path(tmp)
        rc = build_run(work, original)
        if rc:
            raise RuntimeError(f'control failed at runtime: rc={rc}')
        print('GREEN first open/new size/old FD stale/two reconnects stale', flush=True)
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
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_open.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
