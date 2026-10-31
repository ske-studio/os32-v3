"""Product ext2_super.c, Device model, ILP32 and partial I/O contracts."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

CHECK = dict(name='ext2-block-io-host', tier=1, budget=3,
             covers=['fs/ext2/ext2_super.c', 'fs/ext2/ext2_super_block_io.inc',
                     'fs/ext2/ext2_priv.h', 'tools/tests/ext2_block_io_host.c',
                     'tools/tests/test_ext2_block_io.py', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py'], needs=[])
ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-std=gnu11', '-m32', '-march=i386', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-nostdlib', '-static', '-O2',
         '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare',
         '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-D__cdecl=']
MUTANTS = [
    ('partition', 'if (first >= limit || blocks > limit - first) return 0;', '(void)limit;'),
    ('max-blocks', 'blocks > EXT2_DATA_RUN_MAX_BLOCKS', 'blocks > EXT2_DATA_RUN_MAX_BLOCKS + 1'),
    ('overflow-start', 'if (relative > U32_MAX - ctx->base_lba) return 0;', ''),
    ('overflow-end', 'if (sectors - 1 > U32_MAX - *sector) return 0;', '(void)sectors;'),
    ('read-count', 'sector, blocks * EXT2_SECTORS_PER_BLOCK, buf', 'sector, 1, buf'),
    ('write-count', 'sector, blocks * EXT2_SECTORS_PER_BLOCK, src', 'sector, 1, src'),
    ('data-error-state', 'return ext2_raw_read_blocks(ctx, first, blocks, dst);',
     'int ret = ext2_raw_read_blocks(ctx, first, blocks, dst); if (ret) ext2_fs_error(ctx); return ret;'),
    ('partial-error', 'return dev_blk_write_lba(ctx->dev, sector, blocks * EXT2_SECTORS_PER_BLOCK, src);',
     '(void)dev_blk_write_lba(ctx->dev, sector, blocks * EXT2_SECTORS_PER_BLOCK, src); return 0;'),
]


def build(source, exe):
    subprocess.run(['gcc', *FLAGS, *include_flags(ROOT),
                    str(ROOT / 'tools/tests/ext2_block_io_host.c'), str(source),
                    '-o', str(exe)], check=True)


def main(args):
    with tempfile.TemporaryDirectory(prefix='os32-ext2-bio-') as tmp:
        exe = Path(tmp) / 'ext2-block-io'
        build(ROOT / 'fs/ext2/ext2_super.c', exe)
        result = host32.run([str(exe)], cwd=ROOT)
        if result.returncode:
            return result.returncode
        if '--mutate' in args:
            mirror = Path(tmp) / 'fs'
            shutil.copytree(ROOT / 'fs', mirror)
            source = mirror / 'ext2/ext2_super.c'
            path = mirror / 'ext2/ext2_super_block_io.inc'
            original = path.read_text()
            build(source, exe)
            if host32.run([str(exe)], stdout=subprocess.DEVNULL).returncode:
                raise RuntimeError('unchanged mutation fixture failed')
            for name, old, new in MUTANTS:
                if original.count(old) != 1:
                    raise RuntimeError(f'{name}: mutation anchor is not unique')
                path.write_text(original.replace(old, new))
                build(source, exe)
                result = host32.run([str(exe)], stdout=subprocess.PIPE, text=True)
                if result.returncode != 1 or 'FAIL ext2 block I/O' not in result.stdout:
                    raise RuntimeError(f'{name}: expected runtime assertion, got {result.returncode}')
                print(f'MUTANT {name}: runtime RED', flush=True)
                path.write_text(original)
        return 0


def run(ctx):
    import os
    return subprocess.call([sys.executable, '-B', __file__, *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
