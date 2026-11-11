#!/usr/bin/env python3
"""Exercise the ramfs through the real VFS; RAM root coverage belongs to A10."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ram-root', tier=1, budget=60, needs=[],
             covers=['fs/ramfs/ramfs.c', 'tools/tests/test_ram_root.py', 'fs/ramfs/ramfs.h',
                     'fs/vfs.c', 'fs/vfs.h', 'fs/vfs_device.h', 'lib/kstring_c.c'])
SOURCE = r'''
#include "fs/vfs_device.h"
#include "fs/ramfs/ramfs.h"
#include "kstring.h"
static unsigned char heap[32768];
static u32 used;
void *kmalloc(u32 n) { void *p = heap + used; used += (n + 7) & ~7U; return p; }
void *kzalloc(u32 n) { void *p = kmalloc(n); return kmemset(p, 0, n); }
void kfree(void *p) { (void)p; }
void vfs_fd_stale_mount(void *p) { (void)p; }
void vfs_fd_invalidate_mount(void *p) { (void)p; }
static void fail(int code) { __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); __builtin_unreachable(); }
static void item(const VfsDirEntry *e, void *p) {
    int *count = p;
    if (kstrcmp(e->name, "f") || e->size != 2 || e->type != VFS_TYPE_FILE) fail(5);
    ++*count;
}
void _start(void) {
    static const struct vfs_device_name names[] = {{"rd", VFS_NAME_DIGIT_PREFIX, 5, 0, 2}};
    char out[1025]; int count = 0; OS32_Stat st;
    vfs_device_names_register(names, 1);
    if (vfs_register_fs(&ramfs_ops) || vfs_mount("/tmp", "rd1", "ramfs")) fail(2);
    if (vfs_write("/tmp/f", "abc", 3) != 3 || vfs_read("/tmp/f", out, sizeof(out)) != 3 || kstrncmp(out, "abc", 3)) fail(3);
    if (vfs_write("/tmp/f", "xy", 2) != 2 || vfs_read("/tmp/f", out, sizeof(out)) != 2 || kstrncmp(out, "xy", 2)) fail(4);
    if (vfs_ls("/tmp", item, &count) || count != 1 || vfs_stat("/tmp/f", &st) || st.st_size != 2) fail(6);
    if (vfs_write("/tmp/f", out, 1025) != VFS_ERR_NOSPC || vfs_read("/tmp/f", out, sizeof(out)) != 2 || kstrncmp(out, "xy", 2)) fail(7);
    vfs_umount("/tmp"); fail(0);
}
'''


def command(args):
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, timeout=60)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'fs/ramfs/ramfs.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        d = Path(tmp)
        (d / 'test.c').write_text(SOURCE)
        flags = [os.environ.get('CC', 'gcc'), '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
                 '-fno-builtin', '-nostdlib', '-static', '-ffunction-sections', '-fdata-sections',
                 '-Wall', '-Wextra', '-Werror', '-D__cdecl=', '-D__KERNEL_BUILD__',
                 '-I.', '-Iinclude', '-Isdk/include/os32', '-Imm', '-Ilib',
                 '-Wl,--gc-sections']
        for mutant in range(1 + int(args.mutate)):
            source = original
            if mutant:
                old = 'file->size = size;'
                if source.count(old) != 1:
                    raise RuntimeError('mutation anchor is not unique')
                source = source.replace(old, 'file->size = size - 1;')
            (d / 'ramfs.c').write_text(source)
            result = command([*flags, str(d / 'test.c'), str(d / 'ramfs.c'),
                              'fs/vfs.c', 'lib/kstring_c.c', '-o', str(d / 'test')])
            if result.returncode:
                raise RuntimeError(result.stderr)
            result = command(['qemu-i386', str(d / 'test')])
            expected = 3 if mutant else 0
            if result.returncode != expected:
                raise RuntimeError(f'unexpected exit {result.returncode}, expected {expected}: {result.stderr}')
            print('RED written length (exit 3)' if mutant else 'ramfs VFS: OK', flush=True)
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
