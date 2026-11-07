#!/usr/bin/env python3
"""D4/D5 Device media boundary, real ISO/VFS and libc-free ILP32 runtime."""
CHECK = dict(name='hostsvc-media', tier=1, needs=[], budget=10, covers=[
    'fs/isofs/iso9660_media.c', 'fs/isofs/iso9660_media.h',
    'fs/isofs/iso9660.c', 'fs/isofs/iso9660.h', 'fs/isofs/*.inc',
    'fs/vfs.c', 'fs/vfs.h', 'fs/vfs_device.h',
    'fs/vfs_fd.c', 'fs/vfs_fd_stdin.inc', 'fs/vfs_stdin.h', 'exec/exec.h',
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
         '-Werror', '-Wno-unused-parameter', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
MUTATIONS = (
    ('generation', 'if (gen != ctx->media_gen)', 'if (0 && gen != ctx->media_gen)'),
    ('check', 'rc = dev_media_check(dev, &gen);', 'gen = ctx->media_gen; rc = 0;'),
    ('pvd-drop', 'ctx->root_lba = 0;', 'ctx->root_lba = 1;'),
    ('path-drop', 'ctx->pc_valid = 0;', 'ctx->pc_valid = 1;'),
    ('ra-drop', 'ctx->ra_valid = 0;', 'ctx->ra_valid = 1;'),
    ('sector-drop', 'ctx->scache[i].valid = 0;', 'ctx->scache[i].valid = 1;'),
    ('fd-stale', 'vfs_fd_stale_mount(ctx);\n        iso9660_media_drop(ctx);', 'iso9660_media_drop(ctx);'),
    ('atapi-cache', '            media_drop_caches(ctx);\n            ctx->media_gen = gen;',
     '            ctx->media_gen = gen;'),
    ('atapi-fd', 'dev->bus_type == DEV_BUS_ATAPI ? 0 : gen', 'gen'),
    ('atapi-read-result', '    if (rc && dev->bus_type == DEV_BUS_ATAPI &&', '    if (0 && rc && dev->bus_type == DEV_BUS_ATAPI &&'),
    ('read-result', 'if (rc) return rc;\n\n    volume', 'if (rc) return VFS_ERR_IO;\n\n    volume'),
    ('post-read', 'if (after != before)', 'if (0 && after != before)'),
    ('magic', 'pvd[1] != ISO_PVD_MAGIC[0]', '0'),
    ('root', 'sectors > volume - root', '(0 && sectors > volume - root)'),
    ('pending', 'return ctx->block_size ? 0 : OS32_ERR_STALE;', 'return 0;'),
)


# Old generation/retry anchors in test_cd_read.py move to the Device boundary.
OP_MUTATIONS = (
    ('atapi-list-io', 'iso9660.c',
     'if (error == OS32_ERR_STALE && iso_legacy_atapi(ctx)) return VFS_ERR_IO;',
     'if (error == OS32_ERR_STALE && iso_legacy_atapi(ctx)) return OS32_ERR_STALE;'),
    ('atapi-retry', 'iso9660.c',
     'if (before != ctx->media_gen && iso_legacy_atapi(ctx)) continue;',
     'if (before != ctx->media_gen && iso_legacy_atapi(ctx)) return OS32_ERR_STALE;'),
    ('entry-check', 'iso9660.c', 'int ret = iso_check_media(ctx);', 'int ret = iso_check_media(ctx); ret = 0;'),
    ('read-errors', 'iso9660.c', '    return rc;\n}\n\n/* 覚えているもの',
     '    return rc ? VFS_ERR_IO : 0;\n}\n\n/* 覚えているもの'),
    ('read-post', 'iso9660.c', '    if (checked) return checked;\n    if (before != ctx->media_gen) return OS32_ERR_STALE;',
     '    (void)checked;\n    if (before != ctx->media_gen) return OS32_ERR_STALE;'),
    ('extent-fallback', 'iso9660.c', '                if (rc != VFS_ERR_IO) return rc;', ''),
    ('directory-fallback', 'iso9660_dir_read.inc',
     '                if (rc != VFS_ERR_IO) { scan->error = rc; return (const u8 *)0; }', ''),
    ('directory-errors', 'iso9660.c', '            if (!sector) return scan.error;',
     '            if (!sector) return VFS_ERR_IO;'),
    ('sector-errors', 'iso9660_dir_read.inc', 'if (error) *error = rc; return (const u8 *)0; }\n        ctx->stats.scache_fills',
     'if (error) *error = VFS_ERR_IO; return (const u8 *)0; }\n        ctx->stats.scache_fills'),
    ('callback-check', 'iso9660.c',
     '            /* cb がこの FS を読んで媒体の交換を踏んだかもしれない */\n            ret = iso9660_resource_check(ctx, 0);',
     '            ret = 0;'),
    ('mount-retry', 'iso9660.c', 'if (rc != OS32_ERR_STALE) break;', 'break;'),
    ('check-hook', 'iso9660.c', '    iso9660_resource_check,', '    0,'),
    ('refresh-hook', 'iso9660.c', '    iso9660_resource_refresh\n', '    0\n'),
)


WIRING_MUTATIONS = (
    ('u32-priority', 'drivers/base/dev.c', 'if (dev->blk_read_u32)', 'if (0 && dev->blk_read_u32)'),
    ('signed-range', 'drivers/base/dev.c', 'if (lba > S32_MAX || (count && (u32)(count - 1) > S32_MAX - lba))', 'if (0)'),
    ('media-dispatch', 'drivers/base/dev.c', 'if (dev->media_check) return dev->media_check(dev, generation);', 'if (0 && dev->media_check) return dev->media_check(dev, generation);'),
    ('path-check', 'fs/vfs.c', 'rc = ops->resource_check(ctx, &generation);', 'generation = 0; rc = VFS_OK;'),
    ('next-refresh', 'fs/vfs.c', 'if (mnt->refresh_pending && ops->resource_refresh)', 'if (0 && mnt->refresh_pending && ops->resource_refresh)'),
    ('refresh-result', 'fs/vfs.c', 'rc = ops->resource_refresh(ctx);', '(void)ops->resource_refresh(ctx); rc = VFS_OK;'),
    ('dot-error', 'fs/vfs.c', 'if (rc == OS32_ERR_HOSTDOWN || rc == VFS_ERR_STALE) return rc;', ''),
    ('root-error', 'fs/vfs.c', 'if (rc == VFS_ERR_STALE || rc == OS32_ERR_HOSTDOWN) return rc;', ''),
    ('fd-next-path', 'fs/vfs.c', 'mounts[i].refresh_pending = 1;', 'mounts[i].refresh_pending = 0;'),
)


def build_run(work, source, operation=None, wiring=None):
    tree = work / 'fs/isofs'
    tree.mkdir(parents=True, exist_ok=True)
    for name in ('iso9660.c', 'iso9660_dir_read.inc', 'iso9660_dir_record.inc'):
        data = (ROOT / 'fs/isofs' / name).read_text()
        if operation and operation[1] == name:
            _, _, before, after = operation
            if data.count(before) != 1:
                raise RuntimeError(f'{operation[0]}: mutation anchor not unique')
            data = data.replace(before, after)
        (tree / name).write_text(data)
    for rel in ('fs/vfs.c', 'drivers/base/dev.c'):
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        data = (ROOT / rel).read_text()
        if wiring and wiring[1] == rel:
            _, _, before, after = wiring
            if data.count(before) != 1:
                raise RuntimeError(f'{wiring[0]}: mutation anchor not unique')
            data = data.replace(before, after)
        dest.write_text(data)
    harness = work / 'hostsvc_media_host.c'
    harness.write_text((ROOT / 'tools/tests/hostsvc_media_host.c').read_text())
    src = work / 'iso9660_media.c'
    src.write_text(source)
    exe = work / 'media-test'
    subprocess.run(['gcc', *FLAGS, '-I' + str(work), '-I' + str(ROOT),
                    *include_flags(ROOT), '-I' + str(ROOT / 'drivers/base'), '-nostdlib', '-static', '-no-pie',
                    str(src), str(work / 'drivers/base/dev.c'), str(harness),
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-o', str(exe)], check=True, cwd=ROOT)
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
        print('GREEN D4/D5 MEDIA operations/cache/PVD/errors/ATAPI/512-byte device', flush=True)
        if args.mutate:
            for name, before, after in MUTATIONS:
                if original.count(before) != 1:
                    raise RuntimeError(f'{name}: mutation anchor not unique')
                rc = build_run(work, original.replace(before, after))
                if rc <= 0:
                    raise RuntimeError(f'{name}: expected runtime RED, rc={rc}')
                print(f'RED {name}: runtime rc={rc}', flush=True)
            for mutation in OP_MUTATIONS:
                rc = build_run(work, original, mutation)
                if rc <= 0:
                    raise RuntimeError(f'{mutation[0]}: expected runtime RED, rc={rc}')
                print(f'RED {mutation[0]}: runtime rc={rc}', flush=True)
            for mutation in WIRING_MUTATIONS:
                rc = build_run(work, original, wiring=mutation)
                if rc <= 0:
                    raise RuntimeError(f'{mutation[0]}: expected runtime RED, rc={rc}')
                print(f'RED {mutation[0]}: runtime rc={rc}', flush=True)
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
