#!/usr/bin/env python3
"""T5a-1 B2 production facade/dictionary, ordinary failures and lifetime mutants."""
import sqlite_cc
CHECK = dict(name='fep-boundary-host', tier=1,
    covers=['sys/platform/pc98/platform_install_layout.h', 'sys/include/platform_install_boot.h', 'sys/include/ide_contract.h', 'sys/platform/pc98/install_disk.c', 'sys/platform/pc98/install_geometry.h', 'sys/platform/pc98/install_plan.c', 'sys/platform/pc98/install_target.c', 'sys/input/input_context.h', 'sys/include/ext2_medium_contract.h', 'sys/platform/pc98/storage/ext2_medium_ide.c', 'sys/fs/ext2/ext2_time.c', 'tools/tests/sqlite_cc.py', 'sys/kern/ime/ime_display_private.h', 'sys/platform/pc98/platform_ime_render.h', 'sys/platform/pc98/ime_display.c', 'sys/include/ime_control.h', 'tools/tests/fep_boundary_host.c', 'tools/tests/fep_boundary_runtime.h', 'sys/kern/kstring.c', 'sys/kern/kstring_c.c', 'lib/kmath.c', 'lib/divdi3.c', 'tools/tests/ime_dict_host.c', 'tools/tests/vfs_fd_sqlite_host.c', 'tools/tests/sqlite_groups_backend.h', 'tools/tests/test_vfs_fd_path.py', 'tools/tests/host32.py', 'tools/tests/host_arch/**', 'sys/kern/ime/ime.c', 'sys/kern/ime/ime_convert.inc', 'sys/kern/ime/ime_suspend.c', 'sys/kern/ime/ime_romkana.c', 'lib/utf8.c', 'sys/kern/ime/ime.h', 'sys/include/ime_boot.h', 'sys/kern/ime/ime_dict.c', 'sys/kern/ime/ime_conn.c', 'sys/kern/ime/ime_user_store.c', 'tools/tests/fep_store_host.h', 'sys/kern/ime/ime_dict_info.c', 'tools/tests/ime_dict_info_host.inc', 'sys/kern/ime/ime_dict_api.inc', 'sys/kern/ime/fep_module.c', 'sys/kern/ime/fep_body.c', 'sys/kern/ime/fep_module.h', 'sys/kern/ime/ime_store.h', 'sys/kern/kstring.h', 'sys/include/redir_access.h', 'sys/include/io.h', 'sys/fs/vfs_fd.c', 'sys/fs/vfs.h', 'lib/sqlite3/**', 'sdk/kapi.json', 'tools/tests/input_fixture_host.h', 'sys/hal/input/input_service.c', 'sys/hal/input/pointer_service.h', 'sys/hal/input/input_service.h', 'sys/include/kbd.h', 'tools/tests/serial_emergency_host.h', 'sys/kern/serial_diag.c', 'sys/include/console_output.h', 'lib/serial_diag.h', 'tools/tests/kernel_includes.py', 'build/core.yaml', 'sys/dev/block/fdc_track.c', 'sys/fs/pc98pt.c', 'sys/fs/ext2/ext2_dir.c', 'sys/fs/ext2/ext2_file.c', 'sys/fs/ext2/ext2_vfs.c', 'sys/dev/block/fat_diskio.c', 'sys/fs/fat/fatfs/ff.c', 'sys/fs/fat/fatfs_vfs.c', 'sys/dev/hostdrv/hostdrvfs.c', 'sys/fs/isofs/iso9660.c', 'sys/fs/vfs.c', 'lib/kutf16.c', 'sys/platform/pc98/boot_names.c', 'sdk/allocator/nano_adapter.c', 'sdk/crt/syscalls.c', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/tests/mutpar.py', 'tools/tests/newlib_errno_host.c', 'tools/tests/vfs_fd_path_host.c', 'userland/deploy.yaml', 'userland/lib/rt/ls.c', 'userland/shell/hdprep_plan.c', 'userland/system/cdinst.c', 'userland/system/inst_disk.c', 'userland/system/inst_hdd.c'],
    needs=['make:all'], budget=8.3,
    basis=['docs/design/kernel/ime/ime_dict.md 開く', 'docs/design/kernel/ime/ime_dict.md 切替', 'docs/design/kernel/ime/ime_dict_info.md 状態と誤り'],
)

from kernel_includes import include_flags
import argparse
import os
import re
from pathlib import Path
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
MUTANTS = [
    ('ignore-exact-confirm-order', 'sys/kern/ime/ime_dict.c',
     'ORDER BY rank,-COALESCE(freq,0)', 'ORDER BY -COALESCE(freq,0)',
     'latest confirmed spelling leads'),
    ('ignore-confirm-order', 'sys/kern/ime/ime_dict.c',
     'CASE WHEN yomi=?1 THEN ~last_ts END', 'CASE WHEN yomi=?1 THEN 0 END',
     'latest confirmed spelling leads'),
    ('convert-old-owner', 'sys/kern/ime/ime_convert.inc',
     'fep_store_search(&g_ime.store,', 'ime_dict_search(&g_ime.dict,', 'production conversion'),
    ('learn-old-owner', 'sys/kern/ime/ime_convert.inc',
     'fep_store_learn(&g_ime.store,', 'ime_dict_learn(&g_ime.dict,', 'production learning'),
    ('suspend-old-owner', 'sys/kern/ime/ime_suspend.c',
     'fep_store_close(&ime->store)', 'ime_dict_close(&ime->dict)', 'production suspend closes both'),
    ('switch-budget', 'sys/kern/ime/ime_dict.c',
     'target.reserve_kb > s->boot_reserve_kb', '0', 'production switch reservation preserves store'),
    ('switch-old-owner', 'sys/kern/ime/ime_dict_api.inc',
     'fep_store_switch(&g_ime.store, path)', 'ime_dict_reopen(&g_ime.dict, path)', 'production switch'),

    ('boot-config-error', 'sys/kern/ime/ime.c',
     'if (rc) return rc;\n    rc = fep_boot_user_path_get',
     'if (rc) return 0;\n    rc = fep_boot_user_path_get', 'boot config failure'),
    ('boot-live-dict', 'sys/kern/ime/ime.c',
     'return &g_ime.store;', 'return NULL;',
     'boot counts live store dictionary'),
    ('invalid-output-returns-error', 'sys/kern/ime/ime_dict_api.inc',
     '        ring3_fault_kill();\n        return -1;',
     '        return -1;',
     'invalid output must kill'),
    ('failed-kanji-null', 'sys/kern/ime/ime_dict_api.inc',
     'if (!copy_caller_cstr(&caller, kanji, k, sizeof(k))) return -1;\n        kanji = k;',
     'if (!copy_caller_cstr(&caller, kanji, k, sizeof(k))) kanji = NULL;\n        else kanji = k;',
     'invalid input engine0/output unchanged'),
    ('copyout-before-finalize', 'sys/kern/ime/ime_dict_api.inc',
     '    n = fep_store_user_list(&g_ime.store, yomi_prefix, ime_user_staging, max);',
     '    (void)copy_to_caller(&caller, out, ime_user_staging, sizeof(ime_user_staging));\n    n = fep_store_user_list(&g_ime.store, yomi_prefix, ime_user_staging, max);',
     'finalize then leave then copyout'),
    ('late-busy-leak', 'sys/kern/ime/ime_dict_api.inc',
     '(u32)n * sizeof(IME_UserEntry))) n = -1;',
     '(u32)n * sizeof(IME_UserEntry))) return -1;',
     'busy released after late refusal'),
    ('search-borrow-leak', 'sys/kern/ime/ime_dict.c',
     'int clear_rc = sqlite3_bind_null(stmt, 1);', 'int clear_rc = SQLITE_OK;',
     'binding lifetime ended'),
    ('search-reset-leak', 'sys/kern/ime/ime_dict.c',
     'int reset_rc = sqlite3_reset(stmt);\n    int clear_rc',
     'int reset_rc = SQLITE_OK;\n    int clear_rc',
     'reset before input lifetime ends'),
    ('learn-borrow-leak', 'sys/kern/ime/ime_dict.c',
     'int k_rc = sqlite3_bind_null(stmt, 2);', 'int k_rc = SQLITE_OK;',
     'binding lifetime ended'),
    ('info-capacity-overrun', 'sys/kern/ime/ime_dict_info.c',
     'if (n > capacity - 1) n = capacity - 1;',
     'if (n > capacity - 1) n = capacity;', 'metadata chunk cap'),
    ('info-character-slice', 'sys/kern/ime/ime_dict_info.c',
     'kmemcpy(out, (const char *)value + offset, (u32)n);',
     'if (n > 0 && (((const unsigned char *)value)[offset + n - 1] & 0x80)) n = 0;\n        kmemcpy(out, (const char *)value + offset, (u32)n);',
     'metadata full byte reassembly'),
    ('info-notfound-eof', 'sys/kern/ime/ime_dict_info.c',
     'if (rc == SQLITE_DONE) result = OS32_ERR_IO;',
     'if (rc == SQLITE_DONE) result = 0;', 'metadata error code'),
    ('info-io-eof', 'sys/kern/ime/ime_dict_info.c',
     'if (rc) return rc;', 'if (rc) return 0;',
     'metadata error code'),
    ('info-no-stale-check', 'sys/kern/ime/ime_dict_info.c',
     'if (rc) return rc;',
     'if (rc && rc != OS32_ERR_STALE) return rc;', 'metadata error code'),
    ('info-old-variant', 'sys/kern/ime/ime_dict_info.c',
     'if (!kstrcmp(s->system.path, FEP_DICT_PATH_S)) value = "S";',
     'if (!kstrcmp(s->system.path, FEP_DICT_PATH_S)) value = "M";',
     'metadata exact bytes'),
    ('info-stub-success', 'sys/kern/ime/fep_module.c',
     '{ (void)key; (void)offset; (void)out; (void)capacity; return OS32_ERR_NOSYS; }',
     '{ (void)key; (void)offset; (void)out; (void)capacity; return 0; }',
     'metadata absent module stub'),
    ('info-no-offset-bound', 'sys/kern/ime/ime_dict_info.c',
     'if (offset < len) {', 'if (1) {', 'metadata copy bound'),
    ('info-no-public-check', 'sys/kern/ime/ime_dict_info.c',
     'if (!services_public) return OS32_ERR_NOSYS;', '(void)services_public;',
     'metadata error code'),
    ('info-no-output-kill', 'sys/kern/ime/ime_dict_info.c',
     '        ring3_fault_kill();', '', 'metadata invalid output must kill'),
    ('info-ignore-finalize', 'sys/kern/ime/ime_dict_info.c',
     'if (sqlite3_finalize(stmt) != SQLITE_OK) result = OS32_ERR_IO; /* store */',
     '(void)sqlite3_finalize(stmt); /* store */',
     'metadata error code'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('qemu', 'native'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fep-b2-') as directory:
        work = Path(directory)
        cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
        sqlite_cc.build([cc, '-m32', '-march=i386', '-std=gnu89', '-Os', '-DNDEBUG',
                      '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-w',
                      '-fsigned-char', '-fno-short-enums', '-include',
                      str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'), '-c',
                      str(ROOT / 'lib/sqlite3/sqlite3.c'), '-o', str(work / 'sqlite.o')], check=True)
        shim = work / 'ime_shim'
        shim.mkdir()
        (shim / 'memmap.h').write_text('#include "' + str(ROOT / 'sys/include/memmap.h') + '"\n'
            'extern unsigned char test_shm[];\n#undef MEM_SHM_BASE\n#define MEM_SHM_BASE test_shm\n')
        for header in ('stdio.h', 'stdlib.h', 'string.h'):
            (work / header).write_text('#include \"fep_boundary_runtime.h\"\n')
        (work / 'fep_boundary_host.c').write_text(
            (ROOT / 'tools/tests/fep_boundary_host.c').read_text())
        (work / 'ime_dict_info_host.inc').write_text((ROOT / 'tools/tests/ime_dict_info_host.inc').read_text())
        sources = {p: (ROOT / p).read_text() for p in ('sys/kern/ime/ime.c', 'sys/kern/ime/ime_convert.inc', 'sys/kern/ime/ime_suspend.c', 'sys/kern/ime/ime_dict_api.inc', 'sys/kern/ime/ime_dict.c', 'sys/kern/ime/ime_dict_info.c', 'sys/kern/ime/fep_module.c')}
        cases = [('normal', None)]
        if args.mutate:
            cases += [(m[0], m) for m in MUTANTS]
        for name, mutation in cases:
            current = dict(sources)
            if mutation:
                _, path, old, new, marker = mutation
                assert current[path].count(old) == 1, name
                current[path] = current[path].replace(old, new)
            (work / 'ime_source.inc').write_text(current['sys/kern/ime/ime.c'] + '\n#include \"' + str(ROOT / 'sys/kern/ime/fep_body.c') + '\"\n')
            (work / 'ime_convert.inc').write_text(current['sys/kern/ime/ime_convert.inc'])
            (work / 'ime_suspend_source.inc').write_text(current['sys/kern/ime/ime_suspend.c'])
            (work / 'ime_dict_api.inc').write_text(current['sys/kern/ime/ime_dict_api.inc'])
            (work / 'ime_dict.inc').write_text(current['sys/kern/ime/ime_dict.c'])
            (work / 'ime_dict_info.inc').write_text(current['sys/kern/ime/ime_dict_info.c'].replace('../../lib/', str(ROOT / 'lib') + '/'))
            stub = re.search(r'static int stub_dict_info\([^}]+\}', current['sys/kern/ime/fep_module.c']).group(0)
            (work / 'ime_info_stub.inc').write_text(stub + '\n')
            harness = (ROOT / 'tools/tests/ime_dict_host.c').read_text().replace(
                '#include "../../sys/kern/ime/ime_dict.c"', '#include "ime_dict.inc"')
            # Keep other real source includes anchored at the repository.
            harness = harness.replace('#include "../../lib/', '#include "' + str(ROOT / 'lib') + '/')
            start = harness.index('#define __KSTRING_H')
            end = harness.index('#include \"' + str(ROOT / 'lib'), start)
            harness = harness[:start] + 'void *kzalloc(u32 n) { return calloc(1, n); }\nvoid kfree(void *p) { free(p); }\n' + harness[end:]
            harness = harness.replace('int utf8_strlen(const u8 *s)', 'int fixture_utf8_strlen(const u8 *s)')
            (work / 'ime_dict_host.c').write_text(harness)
            exe = work / name
            cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
            host32.build([cc, '-m32', '-march=i386', '-std=gnu11', '-Os', '-nostdlib', '-fno-builtin',
                          '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                          '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-D__cdecl=',
                          '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare',
                          '-Wno-type-limits', '-Wno-missing-field-initializers', '-Wno-maybe-uninitialized',
                          '-I' + str(work), '-I' + str(work / 'ime_shim'),
                          *(['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'tools/tests')] + include_flags(ROOT)),
                          str(work / 'fep_boundary_host.c'),
                          *[str(ROOT / p) for p in ('sys/kern/kstring_c.c', 'sys/kern/kstring.c', 'lib/kmath.c', 'lib/divdi3.c', 'lib/utf8.c', 'sys/kern/ime/ime_romkana.c')], str(work / 'sqlite.o'), '-o', str(exe)], check=True)
            result = host32.run([str(exe)], runner=args.runner, capture_output=True, text=True, timeout=120)
            print(result.stdout + result.stderr, end='', flush=True)
            if mutation:
                assert result.returncode == 1 and marker in result.stderr, (name, result)
                print('RED runtime ' + name, flush=True)
            else:
                assert result.returncode == 0, result
                print('GREEN B2', flush=True)


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_fep_boundary.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
