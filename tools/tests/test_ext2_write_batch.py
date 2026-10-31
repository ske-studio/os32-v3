"""D6 stage 1: product ext2 batching, sector fault images, external fsck oracle."""
from pathlib import Path
import os
import shutil
import subprocess
import struct
import sys
import tempfile
import host32
import b8_e2fsck
from kernel_includes import include_flags

CHECK = dict(name='ext2-write-batch-host', tier=1, budget=900,
             covers=['fs/ext2/**', 'tools/tests/ext2_write_batch_host.c',
                     'tools/tests/test_ext2_write_batch.py', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'tools/tests/hdd_pt_fake.h',
                     'tools/tests/b8_e2fsck.py', 'drivers/block/pc98pt.c',
                     'drivers/block/pc98pt.h', 'include/platform_device.h',
                     'include/ide_contract.h'], needs=[])
ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-std=gnu11', '-m32', '-march=i386', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-nostdlib', '-static', '-O1', '-Wall', '-Wextra',
         '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare', '-D__cdecl=']
CASES = ['counts', 'edges', 'reservations', 'fragment', 'nospace', 'f0', 'f1', 'f2', 'f3', 'f4', 'f5']
MUTANTS = [
    ('bitmap-retain', 'ext2_inode_write_bitmap.inc', 'if (!slot->valid) {',
     'if (1) {', 'counts'),
    ('bitmap-count', 'ext2_inode_write_bitmap.inc',
     'ctx->sb_info.free_blocks_count -= slot->reserved;',
     'ctx->sb_info.free_blocks_count -= 0;', 'counts'),
    ('lower-flush', 'ext2_inode_write_bmap.inc',
     'if (!slot->valid || !slot->dirty) continue;',
     'if (!slot->valid || !slot->dirty || i == 1) continue;', 'counts'),
    ('upper-clean', 'ext2_inode_write_bmap.inc', 'slot->dirty = fresh;',
     'slot->dirty = 1;', 'counts'),
    ('neighbour', 'ext2_inode_write_cache.inc',
     'ext2_inode_encode(slot->raw + slot->offset, &slot->current);',
     'ext2_mem_zero(slot->raw, EXT2_BLOCK_SIZE);\n'
     '    ext2_inode_encode(slot->raw + slot->offset, &slot->current);', 'counts'),
    ('trim', 'ext2_inode_write_bmap.inc',
     'for (i = w->data_done; i < w->data_count; i++)',
     'for (i = w->data_count; i < w->data_count; i++)', 'reservations'),
    ('inode-failure', 'ext2_file_write_batch.inc',
     'if (ext2_write_finish(ctx) != 0) return EXT2_ERR_IO;',
     'if (ext2_write_finish(ctx) != 0) return (int)done;', 'f0'),
]


def build(directory, exe, baseline=False):
    fixture = exe.with_suffix('.c')
    source = (ROOT / 'tools/tests/ext2_write_batch_host.c').read_text()
    fixture.write_text(source.replace('../../fs/ext2/', str(directory) + '/'))
    subprocess.run(['gcc', *FLAGS, *(['-DBASELINE'] if baseline else []),
                    '-I' + str(directory), '-I' + str(ROOT / 'tools/tests'),
                    *include_flags(ROOT), str(fixture), '-o', str(exe)], check=True)


def media_check(image):
    """Independent on-media pointers/bitmaps, including references beyond old EOF."""
    data = image.read_bytes()
    def u16(at):
        return struct.unpack_from('<H', data, at)[0]
    def u32(at):
        return struct.unpack_from('<I', data, at)[0]
    blocks, inodes = u32(1028), u32(1024)
    first, bpg, ipg = u32(1044), u32(1056), u32(1064)
    isize = u16(1112)
    groups = (blocks - first + bpg - 1) // bpg
    gdt_blocks = (groups * 32 + 1023) // 1024
    metadata = set()
    def sparse(g):
        if g in (0, 1):
            return True
        for base in (3, 5, 7):
            n = g
            while n and n % base == 0:
                n //= base
            if n == 1:
                return True
        return False
    for g in range(groups):
        gd = 2048 + g * 32
        if sparse(g):
            metadata.update(range(first + g * bpg, first + g * bpg + 1 + gdt_blocks))
        metadata.update((u32(gd), u32(gd + 4)))
        table = u32(gd + 8)
        metadata.update(range(table, table + (ipg * isize + 1023) // 1024))
    owners = {}
    def claim(block, ino):
        if not first <= block < blocks or block in metadata or block in owners:
            raise AssertionError(f'{image.name}: invalid/shared block {block}, inode {ino}')
        g, bit = divmod(block - first, bpg)
        bitmap = u32(2048 + g * 32) * 1024
        if not data[bitmap + bit // 8] & (1 << (bit % 8)):
            raise AssertionError(f'{image.name}: inode {ino} references free block {block}')
        owners[block] = ino
    def visit(block, depth, ino):
        claim(block, ino)
        if depth:
            for i in range(256):
                child = u32(block * 1024 + i * 4)
                if child:
                    visit(child, depth - 1, ino)
    def inode_at(ino):
        g, index = divmod(ino - 1, ipg)
        return u32(2048 + g * 32 + 8) * 1024 + index * isize
    for ino in range(1, inodes + 1):
        g, bit = divmod(ino - 1, ipg)
        bitmap = u32(2048 + g * 32 + 4) * 1024
        at = inode_at(ino)
        if not data[bitmap + bit // 8] & (1 << (bit % 8)) or not u16(at):
            continue
        for i in range(15):
            block = u32(at + 40 + i * 4)
            if block:
                visit(block, max(0, i - 11), ino)
    # The unrelated named file must survive before and after e2fsck repair.
    root = inode_at(2)
    directory = u32(root + 40) * 1024
    end = directory + 1024
    guard = None
    while directory < end:
        length = u16(directory + 4)
        if length < 8 or directory + length > end:
            raise AssertionError('damaged root directory')
        if data[directory + 8:directory + 8 + data[directory + 6]] == b'guard':
            guard = inode_at(u32(directory))
            break
        directory += length
    if guard is None:
        raise AssertionError('unrelated guard name lost')
    if u32(guard + 4) == 1536:
        got = b''.join(data[u32(guard + 40 + i * 4) * 1024:
                            u32(guard + 40 + i * 4) * 1024 + 1024] for i in range(2))[:1536]
        expected = bytes((i * 7 + i // 257 + 3) & 255 for i in range(1536))
        if got != expected:
            raise AssertionError('unrelated guard content changed')


def fsck(image, clean):
    media_check(image)
    env = dict(os.environ, LC_ALL='C')
    result = subprocess.run(['e2fsck', '-fn', str(image)], capture_output=True,
                            text=True, env=env, timeout=30)
    text = result.stdout + result.stderr
    allowed, bad = b8_e2fsck.classify(text, image)
    if result.returncode not in (0, 4) or bad or (clean and (result.returncode or allowed)):
        raise AssertionError(f'{image.name}: rc={result.returncode} bad={bad}\n{text}')
    if not clean:
        fixed = image.with_suffix('.fixed')
        shutil.copyfile(image, fixed)
        repair = subprocess.run(['e2fsck', '-fy', str(fixed)], capture_output=True,
                                text=True, env=env, timeout=30)
        verify = subprocess.run(['e2fsck', '-fn', str(fixed)], capture_output=True,
                                text=True, env=env, timeout=30)
        media_check(fixed)
        if repair.returncode not in (0, 1) or verify.returncode:
            raise AssertionError(repair.stdout + repair.stderr + verify.stdout + verify.stderr)
    return len(allowed)


def exercise(exe, case, directory):
    proc = subprocess.Popen(host32.command([str(exe), case, str(directory)], cwd=ROOT),
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    samples = repairs = 0
    try:
        for line in proc.stdout:
            if line.startswith('@@IMAGE '):
                clean, stopped = map(int, line.split()[1:])
                repairs += fsck(directory / 'final.img', clean)
                samples += 1
                if stopped:
                    repairs += fsck(directory / 'stop.img', False)
                    samples += 1
                proc.stdin.write('\n'); proc.stdin.flush()
            else:
                print(line, end='', flush=True)
        if proc.wait(timeout=30):
            raise AssertionError('runtime failure: ' + case)
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
    print(f'{case}: e2fsck images={samples}, allowed diagnostics={repairs}', flush=True)


def main(args):
    with tempfile.TemporaryDirectory(prefix='os32-ext2-batch-') as tmp:
        directory = Path(tmp)
        exe = directory / 'batch'
        build(ROOT / 'fs/ext2', exe)
        cases = [a for a in args if not a.startswith('--')] or CASES
        for case in cases:
            exercise(exe, case, directory)
        if '--baseline' in args:
            base = directory / 'base/ext2'
            base.mkdir(parents=True)
            paths = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only',
                                            'ba09ac7c', 'fs/ext2'], text=True).splitlines()
            # The unchanged product baseline must compile and fail the count assertion.
            for path in paths:
                (base / Path(path).name).write_bytes(subprocess.check_output(
                    ['git', 'show', 'ba09ac7c:' + path]))
            # Relative ../vfs.h and ../ide_limits.h remain the product headers.
            for name in ('vfs.h', 'ide_limits.h'):
                shutil.copyfile(ROOT / 'fs' / name, base.parent / name)
            build(base, exe, baseline=True)
            result = host32.run([str(exe), 'counts'], cwd=ROOT, capture_output=True, text=True)
            if result.returncode != 1 or 'g_rd_sect == 12' not in result.stdout:
                raise AssertionError('baseline did not fail at runtime count oracle: ' + result.stdout)
            print('BASELINE runtime RED\n' + result.stdout, flush=True)
        if '--mutate' in args:
            mirror = directory / 'fs'
            shutil.copytree(ROOT / 'fs', mirror)
            product = mirror / 'ext2'
            build(product, exe)
            for case in ('counts', 'reservations'):
                result = host32.run([str(exe), case], cwd=ROOT, capture_output=True, text=True)
                if result.returncode:
                    raise AssertionError('unmodified mutation copy failed: ' + result.stdout)
            for name, file, old, new, case in MUTANTS:
                path = product / file
                original = path.read_text()
                if original.count(old) != 1:
                    raise AssertionError(name + ': nonunique mutation anchor')
                path.write_text(original.replace(old, new))
                build(product, exe)
                result = host32.run([str(exe), case], cwd=ROOT, capture_output=True,
                                    text=True, timeout=60)
                path.write_text(original)
                if result.returncode != 1 or 'FAIL line ' not in result.stdout:
                    raise AssertionError(name + ': expected runtime RED: ' + result.stdout)
                print('MUTANT ' + name + ': runtime RED\n' + result.stdout, flush=True)
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
