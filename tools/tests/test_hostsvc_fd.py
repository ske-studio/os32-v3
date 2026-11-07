#!/usr/bin/env python3
"""D6 FD resource generation with real C1 and libc-free ILP32 runtime."""
CHECK = dict(name='hostsvc-fd', tier=1, needs=[], budget=10, covers=[
    'fs/vfs_fd.c', 'fs/vfs_fd_stdin.inc', 'fs/vfs_stdin.h', 'fs/vfs.h',
    'fs/serialfs/serialfs_persistent.c', 'fs/serialfs/serialfs.h',
    'fs/serialfs/sfs_client.h', 'fs/serialfs/sfs_proto.h',
    'net/host_mailbox.h', 'exec/exec.h',
    'tools/tests/hostsvc_fd_host.c', 'tools/tests/host32.py',
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
         '-Werror', '-Wno-unused-parameter', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
MUTATIONS = (
    ('fd-generation', 'if (generation != f->resource_generation)',
     'if (0 && generation != f->resource_generation)'),
    ('permanent', 'if (f->in_use && f->fs_ctx == ctx) f->stale = 1;',
     'if (f->in_use && f->fs_ctx == ctx) f->stale = 0;'),
    ('open-generation', 'if (after != resource_generation) {',
     'if (0 && after != resource_generation) {'),
    ('open-no-retry', 'goto open_probe;', 'if (0) { goto open_probe; } return VFS_ERR_STALE;'),
    ('open-extra-retry', 'if (retried) return VFS_ERR_STALE;',
     'if (retried > 1) return VFS_ERR_STALE;'),
    ('open-recreate', '(created ? O_CREAT : 0)', '0'),
)


def build_run(work, source):
    src = work / 'vfs_fd.c'
    src.write_text(source)
    exe = work / 'fd-test'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT),
                    *include_flags(ROOT), '-nostdlib', '-static', '-no-pie',
                    '-DPRODUCT_SOURCE="' + str(src) + '"',
                    str(ROOT / 'tools/tests/hostsvc_fd_host.c'),
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-o', str(exe)],
                   check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'fs/vfs_fd.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-fd-') as tmp:
        work = Path(tmp)
        rc = build_run(work, original)
        if rc:
            raise RuntimeError(f'control failed at runtime: rc={rc}')
        print('GREEN D6 FD C1 generation/permanent stale/ctx/errors/close/legacy', flush=True)
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
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_fd.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
