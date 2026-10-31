"""D6 stage 2: physical runs over newly reserved and existing file blocks."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import host32
import test_ext2_write_batch as batch
from kernel_includes import include_flags

CHECK = dict(name='ext2-write-run-host', tier=1, budget=900,
             covers=['fs/ext2/**', 'tools/tests/ext2_write_batch_host.c',
                     'tools/tests/test_ext2_write_batch.py', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'tools/tests/hdd_pt_fake.h',
                     'tools/tests/b8_e2fsck.py', 'drivers/block/pc98pt.c',
                     'drivers/block/pc98pt.h', 'include/platform_device.h',
                     'include/ide_contract.h', 'tools/tests/ext2_write_run_host.c',
                     'tools/tests/test_ext2_write_run.py'], needs=[])
ROOT = batch.ROOT
CASES = ['lengths', 'boundaries', 'f0', 'f1', 'f2', 'f3', 'f4', 'f5', 'f6', 'f7']

MUTANTS = [
    ('one-block', 'blocks < EXT2_DATA_RUN_MAX_BLOCKS &&', 'blocks < 1 &&', 'lengths'),
    ('physical-gap',
     'w->record[w->data_done + blocks].physical == r->physical + blocks)',
     'w->record[w->data_done + blocks].physical != 0)', 'boundaries'),
    ('window-end', 'w->data_count < EXT2_WRITE_WINDOW_BLOCKS)',
     'w->data_count < EXT2_WRITE_WINDOW_BLOCKS - 1)', 'lengths'),
    ('failed-run-counted',
     'if (io != 0) return EXT2_ERR_IO;\n        ext2_write_data_done(ctx, blocks);',
     'ext2_write_data_done(ctx, blocks);\n        if (io != 0) return EXT2_ERR_IO;', 'f0'),
    ('later-data', 'if (io != 0) return EXT2_ERR_IO;',
     'if (io != 0) continue;', 'f0'),
    ('new-edge-zero',
     'if (r->flags & EXT2_WRITE_REC_NEW) ext2_mem_zero(ext2_g_dat, EXT2_BLOCK_SIZE);',
     'if (r->flags & EXT2_WRITE_REC_NEW) { }', 'boundaries'),
]


def build(product, exe):
    fixture = exe.with_suffix('.c')
    fixture.write_text((ROOT / 'tools/tests/ext2_write_run_host.c').read_text())
    (exe.parent / 'ext2_write_batch_host.c').write_text(
        (ROOT / 'tools/tests/ext2_write_batch_host.c').read_text().replace(
            '../../fs/ext2/', str(product) + '/'))
    subprocess.run(['gcc', *batch.FLAGS, '-I' + str(product),
                    '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT),
                    str(fixture), '-o', str(exe)], check=True)


def main(args):
    with tempfile.TemporaryDirectory(prefix='os32-ext2-run-') as tmp:
        directory = Path(tmp)
        exe = directory / 'run'
        build(ROOT / 'fs/ext2', exe)
        for case in [a for a in args if not a.startswith('--')] or CASES:
            batch.exercise(exe, case, directory)
        if '--baseline' in args:
            mirror = directory / 'baseline/fs'
            shutil.copytree(ROOT / 'fs', mirror)
            product = mirror / 'ext2'
            for name in ('ext2_file.c', 'ext2_file_write_batch.inc'):
                (product / name).write_bytes(subprocess.check_output(
                    ['git', 'show', '0b0d46c6:fs/ext2/' + name]))
            build(product, exe)
            result = host32.run([str(exe), 'lengths'], cwd=ROOT,
                                capture_output=True, text=True, timeout=60)
            if result.returncode != 1 or 'data_requests(offset, n * EXT2_BLOCK_SIZE)' not in result.stdout:
                raise AssertionError('baseline did not fail the 64-block runtime oracle: ' + result.stdout)
            print('BASELINE 0b0d46c6 64 new blocks: runtime RED\n' + result.stdout, flush=True)
        if '--mutate' in args:
            mirror = directory / 'mutant/fs'
            shutil.copytree(ROOT / 'fs', mirror)
            product = mirror / 'ext2'
            build(product, exe)
            for case in ('lengths', 'boundaries', 'f0'):
                result = host32.run([str(exe), case], cwd=ROOT, capture_output=True,
                                    text=True, timeout=60)
                if result.returncode:
                    raise AssertionError('mutation control failed: ' + result.stdout)
            path = product / 'ext2_file_write_run.inc'
            original = path.read_text()
            for name, old, new, case in MUTANTS:
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
