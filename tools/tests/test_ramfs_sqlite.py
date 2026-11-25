"""Run the product storage stack against the bundled SQLite on the host."""
import pathlib
import subprocess
import sys
import tempfile

import sqlite_cc
from kernel_includes import include_flags

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main(mutate):
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        obj = str(work / 'sqlite.o')
        sqlite_cc.build(['gcc', '-std=gnu89', '-O0', '-include',
                         str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'), '-c',
                         str(ROOT / 'lib/sqlite3/sqlite3.c'), '-o', obj], check=True)
        source = (ROOT / 'sys/fs/ramfs/ramfs.c').read_text()
        anchor = '.sqlite = &ramfs_sqlite_ops'
        assert source.count(anchor) == 1
        for mutant in range(1 + mutate):
            path = work / 'ramfs.c'
            path.write_text(source.replace(anchor, '.sqlite = 0') if mutant else source)
            exe = work / 'run'
            subprocess.run(['gcc', '-std=gnu11', '-fno-builtin', '-ffunction-sections',
                            '-fdata-sections', '-Wl,--gc-sections', '-D__cdecl=',
                            '-I.', '-Ilib/sqlite3', *include_flags(ROOT),
                            'tools/tests/ramfs_sqlite_host.c', str(path), 'sys/fs/vfs.c',
                            'sys/fs/vfs_fd.c', 'sys/kern/kstring_c.c', 'lib/sqlite3/os32_sqlite_vfs.c',
                            obj, '-o', str(exe)], cwd=ROOT, check=True)
            result = subprocess.run([exe], text=True, stderr=subprocess.PIPE)
            # The normal control always runs; the mutant only adds its own verdict.
            expected = 1 if mutant else 0
            if result.returncode != expected:
                raise RuntimeError(result.stderr)
            if mutant:
                assert 'os32_sqlite_group_open(group' in result.stderr, result
                print('RED runtime missing ramfs capability at DB open')


if __name__ == '__main__':
    main('--mutate' in sys.argv)
