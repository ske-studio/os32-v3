#!/usr/bin/env python3
"""T5a-1 B2 production facade/dictionary, ordinary failures and lifetime mutants."""
CHECK = dict(name='fep-boundary-host', tier=1,
    covers=['tools/tests/fep_boundary_host.c', 'tools/tests/fep_boundary_runtime.h', 'lib/kstring.c', 'lib/kstring_c.c', 'lib/kmath.c', 'lib/divdi3.c', 'tools/tests/ime_dict_host.c', 'tools/tests/vfs_fd_sqlite_host.c', 'tools/tests/sqlite_groups_backend.h', 'tools/tests/test_vfs_fd_path.py', 'tools/tests/host32.py', 'tools/tests/host_arch/**', 'kernel/ime/ime.c', 'kernel/ime/ime.h', 'kernel/ime/ime_dict.c', 'kernel/ime/ime_conn.c', 'kernel/ime/ime_user_store.c', 'tools/tests/fep_store_host.h', 'kernel/ime/ime_dict_info.c', 'tools/tests/ime_dict_info_host.inc', 'kernel/ime/ime_dict_api.inc', 'kernel/ime/fep_module.c', 'kernel/ime/fep_body.c', 'kernel/ime/fep_module.h', 'kernel/ime/ime_store.h', 'lib/kstring.h', 'include/redir_access.h', 'include/io.h', 'fs/vfs_fd.c', 'fs/vfs.h', 'lib/sqlite3/**', 'sdk/kapi.json', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'tools/tests/serial_emergency_host.h', 'kernel/serial_diag.c', 'include/console_output.h', 'lib/serial_diag.h', 'tools/tests/kernel_includes.py', 'build/core.yaml', 'drivers/block/fdc_track.c', 'drivers/block/pc98pt.c', 'fs/ext2/ext2_dir.c', 'fs/ext2/ext2_file.c', 'fs/ext2/ext2_vfs.c', 'fs/fat/fatfs/diskio.c', 'fs/fat/fatfs/ff.c', 'fs/fat/fatfs_vfs.c', 'fs/hostdrv/hostdrvfs.c', 'fs/isofs/iso9660.c', 'fs/vfs.c', 'lib/kutf16.c', 'platform/pc98/boot_names.c', 'sdk/allocator/nano_adapter.c', 'sdk/crt/syscalls.c', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/tests/mutpar.py', 'tools/tests/newlib_errno_host.c', 'tools/tests/vfs_fd_path_host.c', 'userland/deploy.yaml', 'userland/lib/rt/ls.c', 'userland/shell/hdprep_plan.c', 'userland/system/cdinst.c', 'userland/system/inst_disk.c', 'userland/system/inst_hdd.c'],
    needs=['make:all'], budget=4.429502)

from kernel_includes import include_flags
import argparse
import os
import re
from pathlib import Path
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
MUTANTS = [
    ('boot-config-error', 'kernel/ime/ime.c',
     'if (rc) return rc;\n    rc = fep_boot_user_path_get',
     'if (rc) return 0;\n    rc = fep_boot_user_path_get', 'boot config failure'),
    ('boot-live-dict', 'kernel/ime/ime.c',
     'return (const IME_Dict *)&g_ime.store.system;', 'return &g_ime.dict;',
     'boot counts live store dictionary'),
    ('invalid-output-returns-error', 'kernel/ime/ime_dict_api.inc',
     '        ring3_fault_kill();\n        return -1;',
     '        return -1;',
     'invalid output must kill'),
    ('failed-kanji-null', 'kernel/ime/ime_dict_api.inc',
     'if (!copy_caller_cstr(&caller, kanji, k, sizeof(k))) return -1;\n        kanji = k;',
     'if (!copy_caller_cstr(&caller, kanji, k, sizeof(k))) kanji = NULL;\n        else kanji = k;',
     'invalid input engine0/output unchanged'),
    ('copyout-before-finalize', 'kernel/ime/ime_dict_api.inc',
     '    n = ime_user_list(&g_ime.dict, yomi_prefix, ime_user_staging, max);',
     '    (void)copy_to_caller(&caller, out, ime_user_staging, sizeof(ime_user_staging));\n    n = ime_user_list(&g_ime.dict, yomi_prefix, ime_user_staging, max);',
     'finalize then leave then copyout'),
    ('late-busy-leak', 'kernel/ime/ime_dict_api.inc',
     '(u32)n * sizeof(IME_UserEntry))) n = -1;',
     '(u32)n * sizeof(IME_UserEntry))) return -1;',
     'busy released after late refusal'),
    ('search-borrow-leak', 'kernel/ime/ime_dict.c',
     'int clear_rc = sqlite3_bind_null(stmt, 1);', 'int clear_rc = SQLITE_OK;',
     'binding lifetime ended'),
    ('search-reset-leak', 'kernel/ime/ime_dict.c',
     'int reset_rc = sqlite3_reset(stmt);\n    int clear_rc',
     'int reset_rc = SQLITE_OK;\n    int clear_rc',
     'reset before input lifetime ends'),
    ('learn-borrow-leak', 'kernel/ime/ime_dict.c',
     'int k_rc = sqlite3_bind_null(stmt, 2);', 'int k_rc = SQLITE_OK;',
     'binding lifetime ended'),
    ('info-capacity-overrun', 'kernel/ime/ime_dict_info.c',
     'if (n > capacity - 1) n = capacity - 1;',
     'if (n > capacity - 1) n = capacity;', 'metadata chunk cap'),
    ('info-character-slice', 'kernel/ime/ime_dict_info.c',
     'kmemcpy(out, (const char *)value + offset, (u32)n);',
     'if (n > 0 && (((const unsigned char *)value)[offset + n - 1] & 0x80)) n = 0;\n        kmemcpy(out, (const char *)value + offset, (u32)n);',
     'metadata full byte reassembly'),
    ('info-notfound-eof', 'kernel/ime/ime_dict_info.c',
     'if (rc == SQLITE_DONE) result = OS32_ERR_IO;',
     'if (rc == SQLITE_DONE) result = 0;', 'metadata error code'),
    ('info-io-eof', 'kernel/ime/ime_dict_info.c',
     'if (rc) return rc;', 'if (rc) return 0;',
     'metadata error code'),
    ('info-no-stale-check', 'kernel/ime/ime_dict_info.c',
     'if (rc) return rc;',
     'if (rc && rc != OS32_ERR_STALE) return rc;', 'metadata error code'),
    ('info-old-variant', 'kernel/ime/ime_dict_info.c',
     'if (!kstrcmp(s->system.path, FEP_DICT_PATH_S)) value = "S";',
     'if (!kstrcmp(s->system.path, FEP_DICT_PATH_S)) value = "M";',
     'metadata exact bytes'),
    ('info-stub-success', 'kernel/ime/fep_module.c',
     '{ (void)key; (void)offset; (void)out; (void)capacity; return OS32_ERR_NOSYS; }',
     '{ (void)key; (void)offset; (void)out; (void)capacity; return 0; }',
     'metadata absent module stub'),
    ('info-no-offset-bound', 'kernel/ime/ime_dict_info.c',
     'if (offset < len) {', 'if (1) {', 'metadata copy bound'),
    ('info-no-public-check', 'kernel/ime/ime_dict_info.c',
     'if (!services_public) return OS32_ERR_NOSYS;', '(void)services_public;',
     'metadata error code'),
    ('info-no-output-kill', 'kernel/ime/ime_dict_info.c',
     '        ring3_fault_kill();', '', 'metadata invalid output must kill'),
    ('info-ignore-finalize', 'kernel/ime/ime_dict_info.c',
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
            (work / header).write_text('#include \"fep_boundary_runtime.h\"\n')
        (work / 'fep_boundary_host.c').write_text(
            (ROOT / 'tools/tests/fep_boundary_host.c').read_text())
        (work / 'ime_dict_info_host.inc').write_text((ROOT / 'tools/tests/ime_dict_info_host.inc').read_text())
        sources = {p: (ROOT / p).read_text() for p in ('kernel/ime/ime.c', 'kernel/ime/ime_dict_api.inc', 'kernel/ime/ime_dict.c', 'kernel/ime/ime_dict_info.c', 'kernel/ime/fep_module.c')}
        cases = [('normal', None)]
        if args.mutate:
            cases += [(m[0], m) for m in MUTANTS]
        for name, mutation in cases:
            current = dict(sources)
            if mutation:
                _, path, old, new, marker = mutation
                assert current[path].count(old) == 1, name
                current[path] = current[path].replace(old, new)
            (work / 'ime_source.inc').write_text(current['kernel/ime/ime.c'] + '\n#include \"' + str(ROOT / 'kernel/ime/fep_body.c') + '\"\n')
            (work / 'ime_dict_api.inc').write_text(current['kernel/ime/ime_dict_api.inc'])
            (work / 'ime_dict.inc').write_text(current['kernel/ime/ime_dict.c'])
            (work / 'ime_dict_info.inc').write_text(current['kernel/ime/ime_dict_info.c'].replace('../../lib/', str(ROOT / 'lib') + '/'))
            stub = re.search(r'static int stub_dict_info\([^}]+\}', current['kernel/ime/fep_module.c']).group(0)
            (work / 'ime_info_stub.inc').write_text(stub + '\n')
            harness = (ROOT / 'tools/tests/ime_dict_host.c').read_text().replace(
                '#include "../../kernel/ime/ime_dict.c"', '#include "ime_dict.inc"')
            # Keep other real source includes anchored at the repository.
            harness = harness.replace('#include "../../lib/', '#include "' + str(ROOT / 'lib') + '/')
            start = harness.index('#define __KSTRING_H')
            end = harness.index('#include \"' + str(ROOT / 'lib'), start)
            harness = harness[:start] + 'void *kzalloc(u32 n) { return calloc(1, n); }\nvoid kfree(void *p) { free(p); }\n' + harness[end:]
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
                          *[str(ROOT / p) for p in ('lib/kstring_c.c', 'lib/kstring.c', 'lib/kmath.c', 'lib/divdi3.c')], str(work / 'sqlite.o'), '-o', str(exe)], check=True)
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
