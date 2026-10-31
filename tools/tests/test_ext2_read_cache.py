"""D1: count real ext2 Device requests and exercise cache invalidation on RAM media."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

CHECK = dict(name='ext2-read-cache-host', tier=1, budget=90,
             covers=['fs/ext2/*.c', 'fs/ext2/*.inc', 'fs/ext2/*.h',
                     'tools/tests/ext2_read_cache_host.c',
                     'tools/tests/test_ext2_read_cache.py', 'tools/tests/host32.py',
                     'tools/tests/hdd_pt_fake.h', 'tools/tests/kernel_includes.py',
                     'drivers/block/pc98pt.c', 'drivers/block/pc98pt.h', 'include/ide_contract.h',
                     'include/platform_device.h'], needs=[])
ROOT = Path(__file__).resolve().parents[2]
CASES = ['partial', 'single', 'double', 'run33', 'boundaries', 'full',
         'contexts', 'ranges', 'write_failures', 'read_failures', 'lifetime', 'updates']
FLAGS = ['-std=gnu11', '-m32', '-march=i386', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-nostdlib', '-static', '-O1', '-Wall', '-Wextra',
         '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare', '-D__cdecl=']

MUTANTS = [
    ('inode-hit', 'ext2_inode_cache.inc', 'if (enabled && ext2_inode_cache.valid &&',
     'if (0 && enabled && ext2_inode_cache.valid &&', 'partial'),
    ('inode-copy', 'ext2_inode_cache.inc', '*inode = ext2_inode_cache.inode;',
     'inode->size = 0;', 'contexts'),
    ('table-key', 'ext2_inode_indirect_cache.inc', 'slot->block == block',
     '1', 'boundaries'),
    ('upper-slot', 'ext2_inode_indirect_cache.inc',
     'upper ? &ext2_indirect_upper : &ext2_indirect_lower',
     'upper ? &ext2_indirect_lower : &ext2_indirect_lower', 'double'),
    ('data-context', 'ext2_file_read_cache.inc', 'ext2_data_cache.ctx == ctx',
     '1', 'contexts'),
    ('table-context', 'ext2_inode_indirect_cache.inc', 'slot->ctx == ctx',
     '1', 'contexts'),
    ('inode-context', 'ext2_inode_cache.inc', 'ext2_inode_cache.ctx == ctx',
     '1', 'contexts'),
    ('data-hit', 'ext2_file_read_cache.inc', 'ctx->mounted && !ctx->fs_error && ext2_data_cache.valid',
     '0 && ctx->mounted && !ctx->fs_error && ext2_data_cache.valid', 'partial'),
    ('run-one', 'ext2_file_read_run.inc', 'blocks < EXT2_DATA_RUN_MAX_BLOCKS',
     'blocks < 1', 'single'),
    ('run-33', 'ext2_file_read_run.inc', 'blocks < EXT2_DATA_RUN_MAX_BLOCKS',
     'blocks <= EXT2_DATA_RUN_MAX_BLOCKS', 'run33'),
    ('run-gap', 'ext2_file_read_run.inc', 'next == 0 || next != phys + blocks',
     'next == 0', 'ranges'),
    ('inode-reset', 'ext2_inode.c', '    ext2_inode_cache_reset();',
     '    /* mutation: retain inode */', 'write_failures'),
    ('table-reset', 'ext2_inode_indirect_cache.inc', '    ext2_indirect_lower.valid = 0;',
     '    /* mutation: retain lower */', 'write_failures'),
    ('data-reset', 'ext2_file_read_cache.inc', '    ext2_data_cache.valid = 0;\n}',
     '    /* mutation: retain data */\n}', 'write_failures'),
    ('read-reset', 'ext2_super_block_io.inc', 'if (ret != 0) ext2_read_cache_reset();',
     'if (0) ext2_read_cache_reset();', 'read_failures'),
    ('write-reset', 'ext2_super_block_io.inc', '    ext2_read_cache_reset();',
     '    /* mutation: retain on writes */', 'write_failures'),
    ('mount-reset', 'ext2_super.c',
     '    ext2_read_cache_reset();\n    if (ctx->mounted) ext2_unmount(ctx);',
     '    if (ctx->mounted) ext2_unmount(ctx);', 'lifetime'),
    ('unmount-reset', 'ext2_super.c',
     '    ext2_read_cache_reset();\n    ctx->mounted = 0;',
     '    ctx->mounted = 0;', 'lifetime'),
]


def build(directory, exe):
    fixture = Path(exe).with_suffix('.c')
    source = (ROOT / 'tools/tests/ext2_read_cache_host.c').read_text()
    source = source.replace('../../fs/ext2/', str(directory) + '/')
    fixture.write_text(source)
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT), str(fixture), '-o', str(exe)],
                   cwd=ROOT, check=True)


def main(args):
    cases = [a for a in args if a != '--mutate'] or CASES
    with tempfile.TemporaryDirectory(prefix='os32-ext2-cache-') as tmp:
        exe = Path(tmp) / 'cache'
        build(ROOT / 'fs/ext2', exe)
        failed = 0
        for case in cases:
            rc = host32.run([str(exe), case], cwd=ROOT).returncode
            print(f'EXIT {case}={rc}', flush=True)
            failed += rc != 0
        if failed:
            return 1
        if '--mutate' in args:
            mirror = Path(tmp) / 'fs'
            shutil.copytree(ROOT / 'fs', mirror)
            directory = mirror / 'ext2'
            build(directory, exe)
            # A passing unmodified copy is required before runtime kills count.
            for case in CASES:
                result = host32.run([str(exe), case], cwd=ROOT, capture_output=True, text=True)
                if result.returncode:
                    raise RuntimeError('unchanged mutation fixture failed: ' + result.stdout)
            for name, file, old, new, case in MUTANTS:
                path = directory / file
                original = path.read_text()
                expected = 2 if name == 'read-reset' else 3 if name == 'write-reset' else 1
                if original.count(old) != expected:
                    raise RuntimeError(f'{name}: mutation anchor count {original.count(old)} != {expected}')
                path.write_text(original.replace(old, new))
                build(directory, exe)
                result = host32.run([str(exe), case], cwd=ROOT, capture_output=True, text=True)
                path.write_text(original)
                if result.returncode != 1 or 'FAIL line ' not in result.stdout:
                    raise RuntimeError(f'{name}: expected assertion, got {result.returncode}: {result.stdout}')
                print(f'MUTANT {name}: runtime RED', flush=True)
        return 0


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
