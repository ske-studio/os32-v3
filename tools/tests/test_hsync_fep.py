#!/usr/bin/env python3
"""Run real FEP suspension and hsync main on freestanding ILP32 fixtures."""
CHECK = dict(name='hsync-fep-host', tier=1,
    covers=['tools/tests/test_hsync_fep.py', 'tools/tests/hsync_fep_host.c',
            'tools/tests/ime_suspend_host.c', 'tools/tests/fep_suspend_owner_host.c', 'tools/tests/hsync_h2_host.c',
            'tools/tests/hsync_shell_fixture.h', 'tools/tests/fep_boundary_runtime.h',
            'tools/tests/host32.py', 'tools/gen_sqlite_imports.py',
            'tools/gen_kdebug_imports.py', 'tools/prototypes/fep_imports.def',
            'include/**', 'fs/fd_redirect.h', 'fs/vfs.h', 'kernel/sqlite_engine.h',
            'kernel/module/**', 'exec/exec.h', 'drivers/input/kbd.h', 'drivers/char/serial.h',
            'mm/kmalloc.h', 'mm/pgalloc.h', 'lib/kstring.h', 'lib/utf8.h',
            'lib/sqlite3/os32_sqlite_outer.h', 'lib/sqlite3/os32_sqlite_vfs.c',
            'sdk/kapi.json', 'tools/mkmod.py', 'tools/module_schema.py',
            'tools/prototypes/sqlite_imports.def', 'kernel/ime/ime_render.h', 'tools/tests/kernel_includes.py',
            'kernel/ime/ime_suspend.c', 'kernel/ime/ime.h',
            'kernel/ime/fep_module.h', 'kernel/ime/fep_module.c',
            'kernel/ime/fep_body.c', 'build/fep-module.mk',
            'userland/system/hsync.c', 'userland/system/hsync_fep.inc',
            'userland/system/hsync_protect.inc', 'lib/crc32_core.inc',
            'lib/kstring.c', 'lib/kstring_c.c', 'sdk/include/os32/**'],
    needs=['make:all'], budget=60.0)
import argparse
import os
from pathlib import Path
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('missing-settings-db', 'userland/system/hsync.c',
     'if (api->sys_stat(settings, &st) == OS32_ERR_NOTFOUND)',
     'if (0 && api->sys_stat(settings, &st) == OS32_ERR_NOTFOUND)',
     'missing settings continues sync'),
    ('missing-settings-table', 'userland/system/hsync.c',
     'if (rc == DB_STATUS_DONE) {\n        result = hsp_set_fep_user_path(HSP_FEP_USER_PATH);\n        goto finish;',
     'if (rc == DB_STATUS_DONE) {\n        result = -1;\n        goto finish;',
     'missing settings continues sync'),
    ('settings-fallback', 'userland/system/hsync.c',
     '            hsp_fep_all_db = 1;', '            hsp_fep_all_db = 0;',
     'settings failure warns protects continues'),
    ('settings-warning', 'userland/system/hsync.c',
     '                stage, rc);', '                "unknown", rc);',
     'settings failure warns protects continues'),
    ('settings-continue', 'userland/system/hsync.c',
     '            hsp_fep_all_db = 1;', '            hsp_fep_all_db = 1; return 1;',
     'settings failure warns protects continues'),

    ('failed-reopen-retried-at-exit', 'kernel/ime/fep_module.c',
     '    if (!on) suspend_owner = 0;',
     '    if (!on && !rc) suspend_owner = 0;',
     'failed reopen stays disabled without cleanup retry'),
    ('STOP-no-reopen', 'kernel/ime/fep_module.c',
     '        (void)public_api()->dict_suspend(0);',
     '        if (0) (void)public_api()->dict_suspend(0);', 'STOP reopens through owner cleanup'),
    ('abort-no-reopen', 'userland/system/hsync.c',
     '    if (g_abort) {\n        hsync_fep_end();',
     '    if (g_abort) {\n        if (0) hsync_fep_end();', 'fep abort reopens'),
    ('close-failure-continues', 'userland/system/hsync.c',
     '    if (hsync_fep_begin() != 0) {',
     '    if (hsync_fep_begin() != 0 && 0) {', 'fep close failure stops'),
    ('dry-run-closes', 'userland/system/hsync_fep.inc',
     '    if (!g_fep_off || g_dry_run) return 0;',
     '    if (!g_fep_off) return 0;', 'fep dry-run untouched'),
    ('reopen-default-path', 'kernel/ime/ime_suspend.c',
     'fep_store_open(&ime->store, saved_path, saved_user_path)', 'fep_store_open(&ime->store, IME_DICT_PATH, saved_user_path)',
     'dictionary path preserved'),
    ('stale-suspend', 'kernel/ime/ime_suspend.c',
     '    if (suspended && ime->dict_loaded) suspended = 0;',
     '    if (0) suspended = 0;', 'external reopen can suspend again'),
    ('mode-not-restored', 'kernel/ime/ime_suspend.c',
     '    suspended = 0;\n    ime_set_mode(saved_mode);', '    suspended = 0;\n    ime_set_mode(IME_MODE_OFF);', 'restore mode'),
    ('reopen-error-ignored', 'kernel/ime/ime_suspend.c',
     '    if (rc) {\n        kprintf', '    if (rc && 0) {\n        kprintf', 'reopen failure disabled'),
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--runner', choices=('qemu', 'native'), default='qemu')
    args = parser.parse_args()
    paths = ['kernel/ime/fep_module.c', 'kernel/ime/ime_suspend.c', 'userland/system/hsync.c', 'userland/system/hsync_fep.inc']
    originals = {p: (ROOT / p).read_text() for p in paths}
    with tempfile.TemporaryDirectory(prefix='hsync-fep-') as td:
        work = Path(td)
        import subprocess
        subprocess.run(['python3', str(ROOT / 'tools/gen_sqlite_imports.py'),
                        '--module', 'fep', '--out-dir', str(work)], cwd=ROOT, check=True)
        for header in ('stdio.h', 'stdlib.h', 'string.h'):
            (work / header).write_text('#include "fep_boundary_runtime.h"\n')
        fixture = (ROOT / 'tools/tests/hsync_h2_host.c').read_text()
        fixture = fixture.replace('int main(void)', 'int hsync_h2_fixture_main(void)')
        fixture = fixture.replace('../../userland/system/hsync.c', str(work / 'hsync.c'))
        fixture = fixture.replace('    u8 *p = (u8 *)realloc(nd->data, newcap);',
            '    u8 *p = (u8 *)malloc(newcap);\n        if (p && nd->data) memcpy(p, nd->data, nd->cap);')
        fixture = fixture.replace('vsnprintf(line, sizeof(line), fmt, ap)',
                                  'fixture_format(line, sizeof(line), fmt, ap)')
        (work / 'hsync_h2_host.c').write_text(fixture)
        for p in ('hsync_fep_host.c', 'ime_suspend_host.c', 'fep_suspend_owner_host.c'):
            (work / p).write_text((ROOT / 'tools/tests' / p).read_text())
        for mutation in [None] + (MUTATIONS if args.mutate else []):
            current = dict(originals)
            if mutation:
                name, path, old, new, marker = mutation
                assert current[path].count(old) == 1, name
                current[path] = current[path].replace(old, new)
            for path, source in current.items():
                dest = {'ime_suspend.c': 'ime_suspend_source.inc', 'fep_module.c': 'fep_module_source.inc'}.get(Path(path).name, Path(path).name)
                (work / dest).write_text(source.replace('../../lib/', str(ROOT / 'lib') + '/').replace('../../fs/', str(ROOT / 'fs') + '/').replace('../../mm/', str(ROOT / 'mm') + '/').replace('../sqlite_engine.h', str(ROOT / 'kernel/sqlite_engine.h')))
            target = 'ime_suspend_host.c' if mutation and mutation[1].startswith('kernel/') else 'hsync_fep_host.c'
            if mutation and mutation[1].endswith('fep_module.c'):
                target = 'fep_suspend_owner_host.c'
            targets = [target] if mutation else ['ime_suspend_host.c', 'hsync_fep_host.c', 'fep_suspend_owner_host.c']
            for target in targets:
                exe = work / 'test'
                cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
                host32.build([cc, '-m32', '-march=i386', '-std=gnu11', '-Os', '-nostdlib',
                    '-fno-builtin', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-D__cdecl=',
                    '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare',
                    '-I' + str(work), '-I' + str(ROOT / 'tools/tests'),
                    *include_flags(ROOT), '-I' + str(ROOT / 'userland/lib'),
                    '-I' + str(ROOT / 'userland/system'),
                    str(work / target),
                    str(ROOT / 'lib/kstring.c'), str(ROOT / 'lib/kstring_c.c'),
                    '-o', str(exe)], check=True)
                result = host32.run([str(exe)], runner=args.runner, capture_output=True, text=True, timeout=30)
                print(result.stdout + result.stderr, end='', flush=True)
                if mutation:
                    assert result.returncode == 1 and marker in result.stdout, (name, result)
                    print('RED runtime ' + name, flush=True)
                else:
                    assert result.returncode == 0, result

if __name__ == '__main__':
    main()

def run(ctx):
    import subprocess
    for index, runner in enumerate(ctx.runners):
        argv = ['python3', '-B', 'tools/tests/test_hsync_fep.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, close_fds=False)
        if rc:
            return rc
    return 0 if ctx.runners else 1
