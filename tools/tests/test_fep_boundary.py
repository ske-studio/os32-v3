#!/usr/bin/env python3
"""T5a-1 B2 production facade/dictionary, ordinary failures and lifetime mutants."""
import argparse
import os
from pathlib import Path
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
MUTANTS = [
    ('invalid-output-returns-error', 'kernel/ime.c',
     '        ring3_fault_kill();\n        return -1;',
     '        return -1;',
     'invalid output must kill'),
    ('failed-kanji-null', 'kernel/ime.c',
     'if (!copy_caller_cstr(&caller, kanji, k, sizeof(k))) return -1;\n        kanji = k;',
     'if (!copy_caller_cstr(&caller, kanji, k, sizeof(k))) kanji = NULL;\n        else kanji = k;',
     'invalid input engine0/output unchanged'),
    ('copyout-before-finalize', 'kernel/ime.c',
     '    n = ime_user_list(&g_ime.dict, yomi_prefix, ime_user_staging, max);',
     '    (void)copy_to_caller(&caller, out, ime_user_staging, sizeof(ime_user_staging));\n    n = ime_user_list(&g_ime.dict, yomi_prefix, ime_user_staging, max);',
     'finalize then leave then copyout'),
    ('late-busy-leak', 'kernel/ime.c',
     '(u32)n * sizeof(IME_UserEntry))) n = -1;',
     '(u32)n * sizeof(IME_UserEntry))) return -1;',
     'busy released after late refusal'),
    ('search-borrow-leak', 'kernel/ime_dict.c',
     'int clear_rc = sqlite3_bind_null(stmt, 1);', 'int clear_rc = SQLITE_OK;',
     'binding lifetime ended'),
    ('search-reset-leak', 'kernel/ime_dict.c',
     'int reset_rc = sqlite3_reset(stmt);\n    int clear_rc',
     'int reset_rc = SQLITE_OK;\n    int clear_rc',
     'reset before input lifetime ends'),
    ('learn-borrow-leak', 'kernel/ime_dict.c',
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
        sources = {p: (ROOT / p).read_text() for p in ('kernel/ime.c', 'kernel/ime_dict.c')}
        cases = [('normal', None)]
        if args.mutate:
            cases += [(m[0], m) for m in MUTANTS]
        for name, mutation in cases:
            current = dict(sources)
            if mutation:
                _, path, old, new, marker = mutation
                assert current[path].count(old) == 1, name
                current[path] = current[path].replace(old, new)
            (work / 'ime_source.inc').write_text(current['kernel/ime.c'])
            (work / 'ime_dict.inc').write_text(current['kernel/ime_dict.c'])
            harness = (ROOT / 'tools/tests/ime_dict_host.c').read_text().replace(
                '#include "../../kernel/ime_dict.c"', '#include "ime_dict.inc"')
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
                          *['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', 'include',
                            'arch/x86', 'platform/pc98', 'kernel', 'drivers', 'lib', 'lib/sqlite3',
                            'sdk/include/os32', 'exec', 'fs', 'tools/tests')],
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
