#!/usr/bin/env python3
"""T5a-1 B2 production facade/dictionary, ordinary failures and lifetime mutants."""
CHECK = dict(name='fep-boundary-host', tier=1,
    covers=['tools/tests/fep_boundary_host.c', 'tools/tests/ime_dict_host.c', 'tools/tests/vfs_fd_sqlite_host.c', 'tools/tests/sqlite_groups_backend.h', 'tools/tests/fep_budget_linux.c', 'tools/tests/test_vfs_fd_path.py', 'tools/tests/host32.py', 'tools/tests/host_arch/**', 'kernel/ime/ime.c', 'kernel/ime/ime.h', 'kernel/ime/ime_dict.c', 'lib/kstring.h', 'include/redir_access.h', 'include/io.h', 'fs/vfs_fd.c', 'fs/vfs.h', 'lib/sqlite3/**', 'sdk/kapi.json', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'tools/tests/serial_emergency_host.h', 'kernel/serial_diag.c', 'include/console_output.h', 'lib/serial_diag.h', 'tools/tests/kernel_includes.py', 'build/core.yaml', 'drivers/block/fdc_track.c', 'drivers/block/pc98pt.c', 'fs/ext2/ext2_dir.c', 'fs/ext2/ext2_file.c', 'fs/ext2/ext2_vfs.c', 'fs/fat/fatfs/diskio.c', 'fs/fat/fatfs/ff.c', 'fs/fat/fatfs_vfs.c', 'fs/hostdrv/hostdrvfs.c', 'fs/isofs/iso9660.c', 'fs/vfs.c', 'lib/kutf16.c', 'platform/pc98/boot_names.c', 'sdk/allocator/nano_adapter.c', 'sdk/crt/syscalls.c', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/tests/mutpar.py', 'tools/tests/newlib_errno_host.c', 'tools/tests/vfs_fd_path_host.c', 'userland/deploy.yaml', 'userland/lib/rt/ls.c', 'userland/shell/hdprep_plan.c', 'userland/system/cdinst.c', 'userland/system/inst_disk.c', 'userland/system/inst_hdd.c'],
    needs=['make:all'], budget=4.429502)

from kernel_includes import include_flags
import argparse
import os
from pathlib import Path
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
MUTANTS = [
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
        # ime_dict_host suppresses kstring.h's libc declarations. Keep its
        # ASCII constants from the real header without duplicating their values.
        ascii_constants = '\n'.join(line for line in (ROOT / 'lib/kstring.h').read_text().splitlines()
                                    if line.startswith('#define ASCII_'))
        (work / 'fep_boundary_host.c').write_text(ascii_constants + '\n' +
            (ROOT / 'tools/tests/fep_boundary_host.c').read_text())
        sources = {p: (ROOT / p).read_text() for p in ('kernel/ime/ime.c', 'kernel/ime/ime_dict_api.inc', 'kernel/ime/ime_dict.c')}
        cases = [('normal', None)]
        if args.mutate:
            cases += [(m[0], m) for m in MUTANTS]
        for name, mutation in cases:
            current = dict(sources)
            if mutation:
                _, path, old, new, marker = mutation
                assert current[path].count(old) == 1, name
                current[path] = current[path].replace(old, new)
            (work / 'ime_source.inc').write_text(current['kernel/ime/ime.c'])
            (work / 'ime_dict_api.inc').write_text(current['kernel/ime/ime_dict_api.inc'])
            (work / 'ime_dict.inc').write_text(current['kernel/ime/ime_dict.c'])
            harness = (ROOT / 'tools/tests/ime_dict_host.c').read_text().replace(
                '#include "../../kernel/ime/ime_dict.c"', '#include "ime_dict.inc"')
            # Keep other real source includes anchored at the repository.
            harness = harness.replace('#include "../../lib/', '#include "' + str(ROOT / 'lib') + '/')
            (work / 'ime_dict_host.c').write_text(harness)
            exe = work / name
            cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
            host32.build([cc, '-m32', '-march=i386', '-std=gnu11', '-Os', '-nostartfiles',
                          '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                          '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-D__cdecl=',
                          '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare',
                          '-Wno-type-limits', '-Wno-missing-field-initializers', '-Wno-maybe-uninitialized',
                          '-I' + str(work), '-I' + str(work / 'ime_shim'),
                          *(['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'tools/tests')] + include_flags(ROOT)),
                          str(work / 'fep_boundary_host.c'),
                          str(ROOT / 'tools/tests/fep_budget_linux.c'), str(work / 'sqlite.o'),
                          '-lm', '-o', str(exe)], check=True)
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
