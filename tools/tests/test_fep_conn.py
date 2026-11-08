#!/usr/bin/env python3
"""FEP shared connection teardown and readiness, freestanding ILP32."""
CHECK = dict(name='fep-conn', tier=1, needs=[], budget=3.3,
    covers=['kernel/ime/ime_conn.c', 'kernel/ime/ime_store.h', 'kernel/sqlite_engine.h',
            'lib/sqlite3/os32_sqlite_outer.h', 'lib/sqlite3/os32_sqlite_vfs.h',
            'tools/tests/fep_conn_host.c', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk'])
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'kernel/ime/ime_conn.c'
OLD = 'if (vfs_fd_is_stale(fd)) return OS32_ERR_STALE;'
NEW = 'if (vfs_fd_is_stale(fd)) return 0;'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fep-conn-') as tmp:
        work = Path(tmp)
        src = (ROOT / 'tools/tests/fep_conn_host.c').read_text()
        product = (ROOT / PRODUCT).read_text()
        def build(code):
            unit = work / 'conn.c'
            unit.write_text(src.replace('#include "kernel/ime/ime_conn.c"', code))
            exe = work / 'conn'
            subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT / 'kernel/ime'),
                            *include_flags(ROOT), '-nostdlib', '-static', '-no-pie',
                            str(unit), '-o', str(exe)], check=True)
            return host32.run([str(exe)], cwd=ROOT).returncode
        if build(product):
            raise RuntimeError('normal runtime failed')
        print('PASS close order/partial teardown/isolation/readiness/stale; runner=' +
              host32.selected_runner(), flush=True)
        if args.mutate:
            if product.count(OLD) != 1:
                raise RuntimeError('stale anchor not unique')
            rc = build(product.replace(OLD, NEW))
            if rc <= 0:
                raise RuntimeError(f'runtime RED required, rc={rc}')
            print(f'RED stale-before-SQL: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_fep_conn.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
