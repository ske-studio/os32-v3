#!/usr/bin/env python3
"""Stage I real SQLite/VFS search merge and optional user connection."""
CHECK = dict(name='fep-store-search', tier=1, needs=[], budget=15,
    covers=[
        'kernel/ime/ime_dict.c',
        'kernel/ime/ime_conn.c',
        'kernel/ime/ime_user_store.c',
        'kernel/ime/ime_store.h', 'kernel/sqlite_engine.h',
        'kernel/ime/ime.h',
        'kernel/input_service.c',
        'kernel/serial_diag.c',
        'lib/sqlite3/**',
        'lib/kstring_c.c',
        'lib/kstring.c',
        'lib/kmath.c',
        'lib/divdi3.c',
        'lib/kstring.h',
        'fs/vfs_fd.c',
        'fs/vfs.h',
        'sdk/include/os32/os32_kapi_shared.h',
        'include/types.h',
        'tools/tests/ime_dict_host.c',
        'tools/tests/vfs_fd_sqlite_host.c',
        'tools/tests/sqlite_groups_backend.h',
        'tools/tests/fep_store_search_host.c',
        'tools/tests/fep_boundary_runtime.h',
        'tools/tests/host32.py',
        'tools/tests/kernel_includes.py',
        'tools/tests/host_arch/**',
        'tools/tests/input_fixture_host.h',
        'tools/tests/serial_emergency_host.h',
        'build/config.mk',
    ])

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'kernel/ime/ime_dict.c'
# Anchors include their surrounding branch so each mutation is unique.
OLD = '''        if (store_ready(s, 0)) n = store_search_once(s, yomi, out, max, 0, &failed);'''
NEW = '''        if (store_ready(s, 0)) n = 0;'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    with tempfile.TemporaryDirectory(prefix='fep-store-search-') as directory:
        work = Path(directory)
        host32.build([cc, '-m32', '-march=i386', '-std=gnu89', '-Os', '-DNDEBUG',
                      '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-w',
                      '-fsigned-char', '-fno-short-enums', '-include',
                      str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'), '-c',
                      str(ROOT / 'lib/sqlite3/sqlite3.c'), '-o', str(work / 'sqlite.o')], check=True)
        shim = work / 'ime_shim'
        shim.mkdir()
        (shim / 'memmap.h').write_text('#include "' + str(ROOT / 'include/memmap.h') + '"\n'
            'extern unsigned char test_shm[];\n#undef MEM_SHM_BASE\n#define MEM_SHM_BASE test_shm\n')
        for header in ('stdio.h', 'stdlib.h', 'string.h'):
            (work / header).write_text('#include "fep_boundary_runtime.h"\n')
        harness = (ROOT / 'tools/tests/ime_dict_host.c').read_text()
        # This fixture supplies its fault-injectable probe and user product below.
        harness = harness.replace('#include "fep_store_host.h"', '')
        harness = harness.replace('#include "../../kernel/ime/ime_dict.c"',
            'static int stage_i_step(sqlite3_stmt *stmt);\n'
            'static int stage_i_bind_text(sqlite3_stmt *, int, const char *, int, sqlite3_destructor_type);\n'
            'static int stage_i_bind_null(sqlite3_stmt *, int);\n'
            '#define sqlite3_bind_text stage_i_bind_text\n#define sqlite3_bind_null stage_i_bind_null\n'
            '#define sqlite3_step stage_i_step\n#include "product.c"\n#undef sqlite3_step\n#undef sqlite3_bind_text\n#undef sqlite3_bind_null')
        harness = harness.replace('#include "../../lib/', '#include "' + str(ROOT / 'lib') + '/')
        start = harness.index('#define __KSTRING_H')
        end = harness.index('#include "' + str(ROOT / 'lib'), start)
        harness = harness[:start] + 'void *kzalloc(u32 n) { return calloc(1, n); }\nvoid kfree(void *p) { free(p); }\n' + harness[end:]
        (work / 'ime_dict_host.c').write_text(harness)
        (work / 'test.c').write_text((ROOT / 'tools/tests/fep_store_search_host.c').read_text())
        product = (ROOT / PRODUCT).read_text()
        user_product = (ROOT / 'kernel/ime/ime_user_store.c').read_text()
        cases = [('normal', product, user_product, '')]
        if args.mutate:
            if product.count(OLD) != 1:
                raise RuntimeError('system retry anchor not unique')
            cases.append(('system-only-retry', product.replace(OLD, NEW), user_product,
                          'fep_store_search(&s, "か", out, IME_MAX_RESULTS) == 3'))
            old = 'if (i == 2) valid = user_text(p, n, s->meta.dict_id);'
            if user_product.count(old) != 1:
                raise RuntimeError('user identity anchor not unique')
            cases.append(('user-identity', product, user_product.replace(old, 'if (i == 2) valid = 1;'),
                          '!s.user_enabled && !s.user.db && fep_store_live_connections(&s) == 1'))
            old = 'result = user_exec(db, "UPDATE dict_user SET freq=freq WHERE 0");'
            if user_product.count(old) != 1:
                raise RuntimeError('readonly probe anchor not unique')
            cases.append(('readonly-probe', product, user_product.replace(old, 'result = 0;'),
                          '!s.user_enabled && fep_store_live_connections(&s) == 1'))
            old = 'if (fep_conn_close(c)) return 0;\n    int rc = vfs_stat(journal, &st);\n    if (rc != OS32_ERR_NOTFOUND && !(rc == 0 && st.st_size == 0)) {'
            if product.count(old) != 1:
                raise RuntimeError('hot journal anchor not unique')
            cases.append(('hot-journal', product.replace(old, old.replace('if (rc != OS32_ERR_NOTFOUND && !(rc == 0 && st.st_size == 0))', 'if (0)')), user_product,
                          'hits(&s, out) == 3 && !s.user_enabled && !s.user.db'))
            old = 's->boot_reserve_kb && meta.reserve_kb > s->boot_reserve_kb'
            if product.count(old) != 1:
                raise RuntimeError('open reservation anchor not unique')
            cases.append(('resume-budget', product.replace(old, '0'), user_product,
                          'fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == OS32_ERR_AGAIN'))
            old = 'else if (!had_system && !s->user.db && s->user.path[0])'
            if product.count(old) != 1:
                raise RuntimeError('cold switch anchor not unique')
            cases.append(('cold-switch-learning', product.replace(old, 'else if (0)'), user_product,
                          'cold.user_enabled && cold.user.db'))
        for name, code, user_code, assertion in cases:
            (work / 'product.c').write_text(code)
            (work / 'user_product.c').write_text(user_code)
            exe = work / name
            host32.build([cc, '-m32', '-march=i386', '-std=gnu11', '-Os', '-nostdlib', '-fno-builtin',
                          '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                          '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-D__cdecl=',
                          '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare',
                          '-Wno-type-limits', '-Wno-missing-field-initializers', '-Wno-maybe-uninitialized',
                          '-I' + str(work), '-I' + str(shim), '-I' + str(ROOT),
                          '-I' + str(ROOT / 'tools/tests/host_arch'), '-I' + str(ROOT / 'tools/tests'),
                          *include_flags(ROOT), str(work / 'test.c'),
                          *[str(ROOT / p) for p in ('lib/kstring_c.c', 'lib/kstring.c', 'lib/kmath.c', 'lib/divdi3.c')],
                          str(work / 'sqlite.o'), '-o', str(exe)], check=True)
            result = host32.run([str(exe)], capture_output=True, text=True, timeout=120)
            print(result.stdout + result.stderr, end='', flush=True)
            if name == 'normal':
                if result.returncode: raise RuntimeError(f'normal runtime failed rc={result.returncode}')
            elif result.returncode != 1 or assertion not in result.stderr:
                raise RuntimeError(f'{name}: expected runtime assertion, rc={result.returncode}')
            else:
                print('RED runtime ' + name, flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_fep_store_search.py']
        if ctx.mutate and index == 0: argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc: return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
