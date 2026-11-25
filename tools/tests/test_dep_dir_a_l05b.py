"""L05b FS query boundary, value snapshots and copied geometry mutation build.

--prepare builds the substituted copy with -Werror, without running a mutant.
"""
CHECK = dict(name='dep-dir-a-l05b-host', tier=1,
    covers=['sys/include/callconv.h', 'sys/include/types.h', 'sys/include/config.h', 'sys/include/ide_contract.h', 'sys/include/platform_device.h', 'sys/dev/ata/ide.h', 'sys/dev/ide/device_ide.c', 'tools/tests/dep_dir_a_l05b_host.c', 'sys/fs/ext2/ext2_super.c', 'sys/platform/pc98/storage/ext2_medium_ide.c', 'fs/ext2/ext2_fmt_*.inc', 'sys/fs/fat/fatfs_vfs.c', 'sys/dev/block/fat_diskio.c', 'sys/fs/fat/fatfs/diskio_os32.h', 'sys/fs/ide_limits.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=2.8,
    basis=['ARCH_COMPAT_CONTRACT §4.3'],
)

from kernel_includes import include_flags
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
COPIES = ('sys/include/callconv.h', 'sys/include/types.h', 'sys/include/config.h', 'sys/include/ide_contract.h',
          'sys/include/platform_device.h', 'sys/dev/ata/ide.h', 'sys/dev/ide/device_ide.c',
          'tools/tests/dep_dir_a_l05b_host.c')
OLD = 'out->phys_sector_size = info.phys_sector_size;'
NEW = 'out->phys_sector_size = info.phys_sector_size ^ 1u;'
FS = ('sys/platform/pc98/storage/ext2_medium_ide.c', 'sys/fs/fat/fatfs_vfs.c', 'sys/dev/block/fat_diskio.c')


def contracts():
    import re
    for rel in FS:
        source = (ROOT / rel).read_text()
        assert '#include "platform_device.h"' in source, rel
        assert not re.search(r'\b(?:IdeInfo|ide_get_info|ide_drive_present|ide_range_ok)\b',
                             re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)), rel
        assert '"ide.h"' not in source, rel
    for rel in ('sys/fs/ide_limits.h', 'sys/fs/fat/fatfs/diskio_os32.h'):
        source = (ROOT / rel).read_text()
        assert '"ide_contract.h"' in source and '"ide.h"' not in source
    print('PASS FS uses platform value queries and shared IDE constants')


def build(folder, body):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'sys/dev/ide/device_ide.c').write_text(body)
    exe = folder / 'geometry'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector', '-ffunction-sections', '-Wl,--gc-sections',
                    *include_flags(folder),
                    str(folder / 'sys/dev/ide/device_ide.c'),
                    str(folder / 'tools/tests/dep_dir_a_l05b_host.c'), '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    contracts()
    body = (ROOT / 'sys/dev/ide/device_ide.c').read_text()
    assert body.count(OLD) == 1
    with tempfile.TemporaryDirectory(prefix='dep-dir-a-l05b-') as directory:
        folder = Path(directory)
        subprocess.run(['qemu-i386', str(build(folder / 'control', body))], check=True)
        print('PASS geometry all fields, 256/512, absent/errors preserve output; range edge arguments/results')
        if args.prepare:
            build(folder / 'prepared', body.replace(OLD, NEW))
            print('PASS copied headers only; unique anchor; substituted copy builds -Werror (not run)')
        if args.mutate:
            result = subprocess.run(['qemu-i386', str(build(folder / 'mutant', body.replace(OLD, NEW)))])
            assert result.returncode == 1
            print('RED runtime: wrong geometry physical sector size (build succeeded)')


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_a_l05b.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
