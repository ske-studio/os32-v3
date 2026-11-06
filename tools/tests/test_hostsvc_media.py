#!/usr/bin/env python3
"""D4 Device media boundary, real ISO/VFS and libc-free ILP32 runtime."""
CHECK = dict(name='hostsvc-media', tier=1, needs=[], budget=10, covers=[
    'fs/isofs/iso9660_media.c', 'fs/isofs/iso9660_media.h',
    'fs/isofs/iso9660.c', 'fs/isofs/iso9660.h', 'fs/isofs/*.inc',
    'fs/vfs.c', 'fs/vfs.h', 'fs/vfs_device.h',
    'platform/pc98/boot_names.c', 'platform/pc98/boot_names.h',
    'drivers/base/dev.c', 'drivers/base/dev.h',
    'tools/tests/hostsvc_media_host.c', 'tools/tests/host32.py',
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
    ('generation', 'if (gen != ctx->media_gen)', 'if (0 && gen != ctx->media_gen)'),
    ('check', 'rc = dev_media_check(dev, &gen);', 'gen = ctx->media_gen; rc = 0;'),
    ('pvd-drop', 'ctx->root_lba = 0;', 'ctx->root_lba = 1;'),
    ('path-drop', 'ctx->pc_valid = 0;', 'ctx->pc_valid = 1;'),
    ('ra-drop', 'ctx->ra_valid = 0;', 'ctx->ra_valid = 1;'),
    ('sector-drop', 'ctx->scache[i].valid = 0;', 'ctx->scache[i].valid = 1;'),
    ('fd-stale', 'vfs_fd_stale_mount(ctx);\n        iso9660_media_drop(ctx);', 'iso9660_media_drop(ctx);'),
    ('read-result', 'if (rc) return rc;\n\n    volume', 'if (rc) return VFS_ERR_IO;\n\n    volume'),
    ('post-read', 'if (after != before)', 'if (0 && after != before)'),
    ('magic', 'pvd[1] != ISO_PVD_MAGIC[0]', '0'),
    ('root', 'sectors > volume - root', '(0 && sectors > volume - root)'),
    ('pending', 'return ctx->block_size ? 0 : OS32_ERR_STALE;', 'return 0;'),
)




def build_run(work, source):
    src = work / 'iso9660_media.c'
    src.write_text(source)
    exe = work / 'media-test'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT),
                    *include_flags(ROOT), '-nostdlib', '-static', '-no-pie',
                    str(src), str(ROOT / 'drivers/base/dev.c'),
                    str(ROOT / 'tools/tests/hostsvc_media_host.c'),
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-o', str(exe)],
                   check=True, cwd=ROOT)
    return host32.run([str(exe)], cwd=ROOT).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'fs/isofs/iso9660_media.c').read_text()
    with tempfile.TemporaryDirectory(prefix='hostsvc-media-') as tmp:
        work = Path(tmp)
        rc = build_run(work, original)
        if rc:
            raise RuntimeError(f'control failed at runtime: rc={rc}')
        print('GREEN D4 MEDIA generation/cache/PVD/errors/ATAPI/512-byte device', flush=True)
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
        argv = [sys.executable, '-B', 'tools/tests/test_hostsvc_media.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
