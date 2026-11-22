"""APM full-table validation and partition-relative I/O on a disposable disk."""
from pathlib import Path
import subprocess
import sys
import tempfile

import host32

root = Path(__file__).resolve().parents[2]
view = root / 'platform/powermac/partition_view.c'
apm = root / 'platform/powermac/apm.c'
superblock = root / 'fs/ext2/ext2_super.c'
block_io = superblock.with_name('ext2_super_block_io.inc')
cases = [('normal', view, None, None, 0)]
if '--mutate' in sys.argv:
    cases += [('start', view, 'first + offset', 'offset', 5),
              ('last', view, 'count > dev->total_sects - offset', 'count >= dev->total_sects - offset', 6),
              ('early-write', view, '!writable || dev != &view', 'dev != &view', 12),
              ('write-then-error', view, 'if (!writable || dev != &view)',
               'if (!writable) ide_write_sectors(dev->bus_id, first + offset, count, buf);\n'
               '    if (!writable || dev != &view)', 13),
              ('capacity', superblock, 'if (ctx->sb_info.total_blocks > ctx->part_len',
               'if (0 && ctx->sb_info.total_blocks > ctx->part_len', 14),
              ('read-error-reset', block_io,
               'ret = dev_blk_read_lba(ctx->dev, sector, blocks * EXT2_SECTORS_PER_BLOCK, buf);\n'
               '    if (ret != 0) ext2_read_cache_reset();',
               'ret = dev_blk_read_lba(ctx->dev, sector, blocks * EXT2_SECTORS_PER_BLOCK, buf);', 17),
              ('descriptor-error', superblock,
               'ret = ext2_read_block(ctx, gd_block, ext2_g_blk);\n                if (ret != 0) return EXT2_ERR_IO;',
               'ret = ext2_read_block(ctx, gd_block, ext2_g_blk);\n                if (ret != 0) return EXT2_ERR_NOPART;', 16),
              ('type', apm, 'i + 1 == number && kstrncmp', '0 && kstrncmp', 11),
              ('magic', apm, 'return EXT2_ERR_MAGIC;', 'block[0] = 0;', 10)]
with tempfile.TemporaryDirectory() as tmp:
    for name, target, old, new, expected in cases:
        source = target.read_text()
        if old:
            assert source.count(old) == 1
            source = source.replace(old, new)
        case_dir = Path(tmp) / name
        case_dir.mkdir()
        path = case_dir / target.name
        path.write_text(source)
        sources = [apm, view, superblock]
        if target == block_io:
            # Quoted includes resolve beside the translation unit first.
            local_super = case_dir / superblock.name
            local_super.write_text(superblock.read_text())
            sources[2] = local_super
        else:
            sources[sources.index(target)] = path
        exe = Path(tmp) / 'host'
        subprocess.run(['cc', '-m32', '-nostdinc', '-nostdlib', '-static', '-ffreestanding', '-fno-pie', '-ffunction-sections', '-fdata-sections',
                        '-Wl,--gc-sections', '-DOS32_FEATURE_IDE=1', '-Ifs/ext2', '-D__cdecl=', '-I.', '-Iinclude', '-Iplatform/powermac',
                        '-Ikernel/dev', '-Ilib', '-Isdk/include/os32', '-Iarch/ppc',
                        'tools/tests/ppc_partition_host.c', *sources, 'platform/powermac/root_boot.c',
                        'lib/kstring_c.c', '-o', exe], cwd=root, check=True)
        rc = host32.run([exe]).returncode
        assert rc == expected, (name, rc, expected)
        print(name, 'RED' if rc else 'OK', rc)
