#!/usr/bin/env python3
"""Exercise static dictionary boot, cleanup and retry through the public entry."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='fep-static-host', tier=1, needs=[], budget=12,
    covers=['tools/tests/fep_static_host.c', 'tools/tests/test_fep_static.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk',
            'tools/tests/fep_boundary_runtime.h', 'kernel/ime/*.h', 'kernel/ime/*.inc',
            'kernel/ime/ime.c', 'kernel/ime/ime_suspend.c', 'kernel/ime/fep_static.c',
            'kernel/ime/fep_suspend_owner.c', 'kernel/ime/fep_boot_config.c',
            'kernel/sqlite_engine.h', 'kapi/kapi_db.c', 'kapi/kapi_db.h', 'kapi/kapi_db*.inc', 'lib/utf8.c', 'lib/kstring.c', 'lib/kstring_c.c',
            'lib/sqlite3/generated/sqlite_facade.h', 'fs/vfs.h'],
    basis=['docs/design/kernel/ime/ime_store.md 静的 FEP の辞書起動'])
MUTATIONS = [
    ('switch-gate', 'if (gate_rc) return gate_rc;', 'if (0) return gate_rc;'),
    ('suspend-gate', 'if (rc) return rc;\n    rc = suspend_dictionary(on);', 'rc = suspend_dictionary(on);'),
    ('loaded-fast-path', 'if (g_ime.dict_loaded) return 0;\n    rc = fep_static_prepare();',
     'rc = fep_static_prepare();'),
    ('mount', '!vfs_is_mounted("/db")', '0'),
    ('generation', 'id.mount_generation != generation', '0'),
    ('failed-create-delete', 'return optional_error(edition, dst, 1);',
     'vfs_rm(edition->path); return optional_error(edition, dst, 1);'),
    ('failed-delete', 'rc && rc != VFS_ERR_NOTFOUND', '0'),
    ('pending-lifetime', 'FepMeta meta;\n    state', 'FepMeta meta;\n    pending = 0;\n    state'),
    ('identity', 'id.file_id == copied.file_id', '1'),
    ('probe-close', 'return os32_sqlite_outer_boot_check(0, 0) ?',
     'return os32_sqlite_outer_boot_check(0, 0) == SQLITE_BUSY ?'),
    ('variants', '!rc && i < sizeof(editions)', '!editions[0].done && !rc && i < sizeof(editions)'),
    ('fstype', 'kstrcmp(vfs_fstype("/db"), "ramfs")', '0'),
    ('backend', 'vfs_sqlite_backend(IME_DICT_PATH) <= 0', '0'),
    ('mount-path', 'vfs_path_mount_generation(edition->path) != generation', '0'),
    ('identity-read', 'vfs_sqlite_fd_identity(dst, &id) ||', '(vfs_sqlite_fd_identity(dst, &id), 0) ||'),
    ('stat-error', 'if (disconnected(rc)) return OS32_ERR_AGAIN;\n    if (rc != VFS_ERR_NOTFOUND)',
     'if (rc != VFS_ERR_NOTFOUND)'),
    ('source-absent', 'if (src == VFS_ERR_NOTFOUND)', 'if (0)'),
    ('source-retry', 'if (disconnected(src))', 'if (0)'),
    ('source-error', 'if (src < 0) return optional_error(edition, src, 0);',
     'if (src < 0) return optional_error(edition, OS32_ERR_INVAL, 1);'),
    ('short-write', 'written < 0 ? written : VFS_ERR_IO', 'written'),
    ('copy-retry', 'if (disconnected(rc)) return OS32_ERR_AGAIN;\n        return optional_error',
     'return optional_error'),
    ('reserve-memory', 'rc == SQLITE_NOMEM ? OS32_ERR_NOSPC : OS32_ERR_IO', 'OS32_ERR_IO'),
    ('reserve-error', 'rc == SQLITE_NOMEM ? OS32_ERR_NOSPC : OS32_ERR_IO', 'OS32_ERR_NOSPC'),
    ('probe-pending', 'pending = vfs_sqlite_fd_identity(dst, &copied) == 0;', 'pending = 0;'),
    ('probe-no-identity', 'pending = vfs_sqlite_fd_identity(dst, &copied) == 0;',
     'pending = (vfs_sqlite_fd_identity(dst, &copied), 1);'),
    ('probe-invalid', 'rc != OS32_ERR_INVAL || !pending', 'rc != OS32_ERR_INVAL'),
    ('identity-generation', 'id.mount_generation == copied.mount_generation &&', ''),
    ('identity-unavailable', 'same = !vfs_sqlite_fd_identity(fd, &id) &&', 'same = vfs_sqlite_fd_identity(fd, &id) ||'),
    ('load-positive', 'open_rc < 0 ? open_rc : OS32_ERR_IO', 'open_rc ? open_rc : OS32_ERR_IO'),
    ('load-negative', 'open_rc < 0 ? open_rc : OS32_ERR_IO', 'OS32_ERR_IO'),
    ('commit-check', 'if (!open_rc && !os32_sqlite_outer_boot_check(ime_boot_connections(), reserve_bytes))',
     'if (!open_rc)'),
    ('release', 'ime_boot_abort() || os32_sqlite_outer_boot_check(0, 0)', 'ime_boot_abort()'),
    ('commit-public', 'services_stage = SERVICES_READY;', 'services_stage = SERVICES_SQLITE_ONLY;'),
    ('reserve-rollback', 'if (rc) return failed(rc == SQLITE_NOMEM',
     'if (rc) ime_boot_abort();\n    if (rc) return failed(rc == SQLITE_NOMEM'),
    ('abort', 'ime_boot_abort() ||', '(ime_boot_abort(), 0) ||'),
    ('loaded-gate', 'int rc = fep_static_gate();', 'if (g_ime.dict_loaded) return 0;\n    int rc = fep_static_gate();'),
    ('copy-cleanup', 'rc = remove_copy(edition, rc);', '(void)rc;'),
    ('optional-retry', 'edition->skip = skip;', 'edition->skip = 0;'),
    ('initial-busy', 'if (rc == SQLITE_BUSY || rc == SQLITE_MISUSE) return failed(OS32_ERR_AGAIN);\n    if (rc) return failed(OS32_ERR_NOSYS);',
     'if (rc == SQLITE_BUSY) ime_boot_abort();\n    if (rc == SQLITE_BUSY || rc == SQLITE_MISUSE) return failed(OS32_ERR_AGAIN);\n    if (rc) return failed(OS32_ERR_NOSYS);'),
]

def exercise(mutate, runner=None):
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    ime = ROOT / 'kernel/ime'
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        flags = [cc, '-std=gnu11', '-Os', '-ffreestanding', '-fno-builtin', '-nostdlib',
                 '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                 '-Werror=implicit-function-declaration', '-D__cdecl=', '-DOS32_FEP_STATIC',
                 '-I'+str(work), '-Itools/tests', '-Ilib/sqlite3', '-iquote', str(ime),
                 *include_flags(ROOT)]
        source_names = ('fep_static.c', 'ime.c', 'ime_dict_api.inc', 'ime_suspend.c')
        originals = {name: (ime / name).read_text() for name in source_names}
        overrides = {'switch-gate': 'ime_dict_api.inc', 'suspend-gate': 'ime_suspend.c',
                     'loaded-gate': 'ime.c', 'loaded-fast-path': 'ime.c'}
        units = ['tools/tests/fep_static_host.c', work / 'ime.c', work / 'ime_suspend.c',
                 ime / 'fep_suspend_owner.c', ime / 'fep_boot_config.c', 'kapi/kapi_db.c',
                 'lib/utf8.c', 'lib/kstring.c', 'lib/kstring_c.c']
        affected = dict(zip(source_names, [units[0], units[1], units[1], units[2]]))
        for entry in [None] + (MUTATIONS if mutate else []):
            versions = originals.copy()
            if entry:
                name, old, new = entry
                source = overrides.get(name, 'fep_static.c')
                assert versions[source].count(old) == 1, name
                versions[source] = versions[source].replace(old, new)
            for source_name, code in versions.items():
                filename = 'fep_static_source.inc' if source_name == 'fep_static.c' else source_name
                (work / filename).write_text(code)
            objects = []
            for unit in units:
                obj = work / (Path(unit).stem + '.o')
                if entry and unit == affected[source]:
                    obj = work / 'mutant.o'
                if not entry or unit == affected[source]:
                    subprocess.run([*flags, '-c', str(unit), '-o', str(obj)], cwd=ROOT, check=True)
                objects.append(str(obj))
            exe = work / 'test'
            subprocess.run([cc, '-nostdlib', '-Wl,--gc-sections', *objects, '-o', str(exe)],
                           cwd=ROOT, check=True)
            r = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
            if r.returncode != int(bool(entry)) or (entry and 'FAIL row' not in r.stdout):
                raise RuntimeError((entry, r.returncode, r.stdout, r.stderr))
            print('RED ' + name + ' ' + r.stdout.strip() if entry else 'PASS rows 1-42 and mount exits')

def run(ctx):
    for index, runner in enumerate(ctx.runners):
        exercise(ctx.mutate and index == 0, runner)
    return 0

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    exercise(parser.parse_args().mutate)
