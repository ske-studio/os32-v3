#!/usr/bin/env python3
"""Learning DB open/schema/meta fault injection, freestanding ILP32."""
CHECK = dict(name='fep-user-store', tier=1, needs=[], budget=8,
    covers=['kernel/ime/ime_user_store.c', 'kernel/ime/ime_conn.c',
            'kernel/ime/ime_store.h', 'lib/sqlite3/os32_sqlite_outer.h',
            'lib/sqlite3/os32_sqlite_vfs.h', 'lib/kstring_c.c',
            'tools/tests/fep_user_store_host.c', 'tools/tests/fep_store_fixture.c',
            'tools/tests/fep_store_fixture.h', 'tools/tests/host32.py',
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
PRODUCT = 'kernel/ime/ime_user_store.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
MUTANTS = [
    ('path-directory', 'if (len <= dir || kstrncmp(path, FEP_USER_DIR, dir)) return 0;',
     'if (len <= dir) return 0;'),
    ('path-child', "if (path[i] == '/' || path[i] == '\\\\') return 0;",
     "if (path[i] == '\\\\') return 0;"),
    ('journal-boundary', 'len + kstrlen(FEP_USER_JOURNAL_SUFFIX) >= OS32_MAX_PATH',
     'len + kstrlen(FEP_USER_JOURNAL_SUFFIX) > OS32_MAX_PATH'),
    ('format-version', 'valid = user_number(p, n, FEP_DICT_FORMAT_VERSION);',
     'valid = 1;'),
    ('user-kind', 'valid = user_text(p, n, "user");', 'valid = 1;'),
    ('dictionary-id', 'valid = user_text(p, n, s->meta.dict_id);', 'valid = 1;'),
    ('pos-version', 'valid = user_number(p, n, s->meta.pos_version);', 'valid = 1;'),
    ('created', 'valid = user_created(p, n);', 'valid = user_created(p, n) || n;'),
    ('numeric-overflow', 'if (value > (0xffffffffu - digit) / 10u) return 0;',
     'if (value > (0xffffffffu - digit) / 10u) value = value;'),
    ('page-size', 'sqlite3_column_int(stmt, 0) != FEP_DICT_PAGE_SIZE',
     'sqlite3_column_int(stmt, 0) == FEP_DICT_PAGE_SIZE'),
    ('readonly', '(rc & 255) == SQLITE_READONLY ? OS32_ERR_ROFS : OS32_ERR_IO',
     '(rc & 255) == SQLITE_READONLY ? OS32_ERR_IO : OS32_ERR_IO'),
    ('rollback', 'int rollback = user_exec(db, "ROLLBACK");\n        if (rollback) return rollback;', 'int rollback = user_exec(db, "COMMIT");\n        if (rollback) return rollback;'),
    ('commit', 'if (!result) result = user_exec(db, "COMMIT");',
     'if (!result) result = user_exec(db, "ROLLBACK");'),
    ('mismatch-close', 'rc = fep_conn_close(c);', 'rc = result ? 0 : fep_conn_close(c);'),
    ('close-reason', 'return rc ? rc : result;', 'return result;'),
    ('user-enabled', 's->user_enabled = 1; return 0;', 's->user_enabled = 0; return 0;'),
    ('meta-missing', 'result = user_sql_error("meta missing/read", rc);',
     'result = OS32_ERR_INVAL;'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fep-user-store-') as tmp:
        work = Path(tmp)
        src = (ROOT / 'tools/tests/fep_user_store_host.c').read_text()
        product = (ROOT / PRODUCT).read_text()
        def build(code, mutant=False):
            unit = work / 'user.c'
            unit.write_text(src.replace('#include "kernel/ime/ime_user_store.c"', code))
            exe = work / 'user'
            subprocess.run(['gcc', *FLAGS, *(['-Wno-unused-function'] if mutant else []),
                            '-I' + str(ROOT / 'kernel/ime'), '-I' + str(ROOT),
                            *include_flags(ROOT), '-nostdlib', '-static', '-no-pie',
                            str(unit), str(ROOT / 'lib/kstring_c.c'), '-o', str(exe)], check=True)
            return host32.run([str(exe)], cwd=ROOT).returncode
        rc = build(product)
        if rc:
            raise RuntimeError(f'normal runtime failed rc={rc}')
        print('PASS user path/meta/schema/create/rollback/close; runner=' +
              host32.selected_runner(), flush=True)
        if args.mutate:
            for name, old, new in MUTANTS:
                if product.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor not unique')
                rc = build(product.replace(old, new), mutant=True)
                if rc <= 0:
                    raise RuntimeError(f'{name}: runtime RED required, rc={rc}')
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_fep_user_store.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
