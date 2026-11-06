#!/usr/bin/env python3
"""FEP system metadata reader and caller boundary, freestanding ILP32."""
CHECK = dict(name='fep-store-info', tier=1, needs=[], budget=5,
    covers=['kernel/ime/ime_dict_info.c', 'kernel/ime/ime.c',
            'kernel/ime/ime_conn.c', 'kernel/ime/ime.h', 'kernel/ime/ime_store.h',
            'tools/tests/fep_store_info_host.c', 'tools/tests/fep_store_fixture.c',
            'tools/tests/fep_store_fixture.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk',
            'lib/sqlite3/os32_sqlite_outer.h', 'lib/sqlite3/os32_sqlite_vfs.h',
            'include/redir_access.h', 'sdk/include/os32/os32_kapi_shared.h'])
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
PRODUCT = 'kernel/ime/ime_dict_info.c'
# Each product anchor is unique; compile failures never count as runtime RED.
MUTANTS = [
    ('capacity', PRODUCT, 'if (n > capacity - 1) n = capacity - 1;', 'if (n > capacity - 1) n = capacity;', 'chunk bounds'),
    ('byte-offset', PRODUCT, '(const char *)value + offset', '(const char *)value', 'UTF-8 full reassembly'),
    ('missing-meta', PRODUCT, 'if (rc == SQLITE_DONE) result = OS32_ERR_IO;', 'if (rc == SQLITE_DONE) result = 0;', 'required missing key'),
    ('finalize', PRODUCT, 'if (sqlite3_finalize(stmt) != SQLITE_OK) result = OS32_ERR_IO;', '(void)sqlite3_finalize(stmt);', 'finalize error preserves out'),
    ('legacy-variant', PRODUCT, 'if (!kstrcmp(s->system.path, FEP_DICT_PATH_S)) value = "S";', 'if (!kstrcmp(s->system.path, FEP_DICT_PATH_S)) value = "M";', 'legacy S'),
    ('system-only', PRODUCT, 'ime_meta_read(&s->system, key, offset, staging, capacity)', 'ime_meta_read(&s->user, key, offset, staging, capacity)', 'system handle'),
    ('public-gate', PRODUCT, 'if (!services_public) return OS32_ERR_NOSYS;', '(void)services_public;', 'publication ordering'),
    ('output-kill', PRODUCT, '        ring3_fault_kill();', '        (void)out;', 'invalid caller range'),
    ('stale', 'kernel/ime/ime_conn.c', 'if (vfs_fd_is_stale(fd)) return OS32_ERR_STALE;', 'if (vfs_fd_is_stale(fd)) return 0;', 'no SQL after stale'),
    ('current-view', 'kernel/ime/ime.c', 'const FepConn *conn = &g_ime.store.system;', 'const FepConn *conn = &g_ime.store.user;', 'getter system view'),
    ('current-store', 'kernel/ime/ime.c', 'FepStore *ime_current_store(void) { return &g_ime.store; }', 'FepStore *ime_current_store(void) { return 0; }', 'facade owns current store'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fep-store-info-') as tmp:
        work = Path(tmp)
        src = (ROOT / 'tools/tests/fep_store_info_host.c').read_text()
        paths = [PRODUCT, 'kernel/ime/ime_conn.c', 'kernel/ime/ime.c']
        products = {p: (ROOT / p).read_text() for p in paths}
        def build(codes):
            main = codes['kernel/ime/ime.c']
            getters = main[main.index('const IME_Dict *ime_current_dict(void)'):main.index('static int g_ime_reader_owner;')]
            unit = work / 'info.c'
            code = src.replace('/* CURRENT_GETTERS */', getters)
            for marker, path in [('PRODUCT_INFO', PRODUCT), ('PRODUCT_CONN', 'kernel/ime/ime_conn.c')]:
                code = code.replace('/* ' + marker + ' */', codes[path].replace('../../lib/', str(ROOT / 'lib') + '/'))
            unit.write_text(code)
            exe = work / 'info'
            subprocess.run(['gcc', *FLAGS, '-ffunction-sections', '-fdata-sections',
                            '-I' + str(ROOT), '-I' + str(ROOT / 'kernel/ime'),
                            *include_flags(ROOT), '-nostdlib', '-static', '-no-pie',
                            '-Wl,--gc-sections', str(unit), '-o', str(exe)], check=True)
            return host32.run([str(exe)], cwd=ROOT).returncode
        if build(products):
            raise RuntimeError('normal runtime failed')
        print('PASS 20 keys/legacy/byte chunks/errors/stale/caller/getters; runner=' +
              host32.selected_runner(), flush=True)
        if args.mutate:
            for name, path, old, new, test in MUTANTS:
                if products[path].count(old) != 1:
                    raise RuntimeError(name + ': anchor not unique')
                changed = dict(products)
                changed[path] = changed[path].replace(old, new)
                rc = build(changed)
                if rc <= 0:
                    raise RuntimeError(f'{name}: runtime RED required, rc={rc}')
                print(f'RED {name}: runtime rc={rc}; {test}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_fep_store_info.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
