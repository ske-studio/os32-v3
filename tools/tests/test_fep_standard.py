#!/usr/bin/env python3
"""T4c-3: app pressure at the product ILP32 cap and standard ext2 RW learning."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'tools/tests/fep_standard_host.c'
# Only changed implementation sources need overlay copies; other includes
# fall back to ROOT. All regressions run normally before any mutant.
FILES = [SOURCE, 'lib/sqlite3/os32_sqlite_vfs.c', 'kernel/ime/ime_dict.c']
RESERVE_FLOOR = r'''        if (reserve > 0 && reserve < OS32_SQLITE_FEP_RESERVE_MIN) {
            kprintf(KLOG_ATTR, "[SQT] FEP reserve below minimum; using %d bytes\n",
                    OS32_SQLITE_FEP_RESERVE_MIN);
            reserve = OS32_SQLITE_FEP_RESERVE_MIN;
        }
        if (reserve < OS32_SQLITE_FEP_RESERVE_MIN ||'''
MUTANTS = [
    ('reserve_reject_lower', 'reserve', 'lib/sqlite3/os32_sqlite_vfs.c',
     RESERVE_FLOOR, RESERVE_FLOOR.replace('OS32_SQLITE_FEP_RESERVE_MIN', 'OS32_SQLITE_FEP_RESERVE'),
     'va_arg(args, int) == OS32_SQLITE_FEP_RESERVE_MIN'),
    ('reserve_no_minimum', 'reserve', 'lib/sqlite3/os32_sqlite_vfs.c',
     RESERVE_FLOOR, '        if (reserve < 0 ||',
     'budget->reserve == cases[i].adopted'),
    ('fep_uses_app_cap', 'pressure', 'lib/sqlite3/os32_sqlite_vfs.c',
     'if (budget->context == BUDGET_APP &&',
     'if ((budget->context == BUDGET_APP || budget->context == BUDGET_FEP) &&',
     'ime_dict_search(dict, "か", result, IME_MAX_RESULTS) == 2'),
    ('skip_learning', 'learning', 'kernel/ime/ime_dict.c',
     'if (rc == SQLITE_OK) rc = sqlite3_step(stmt);\n    int reset_rc',
     'if (rc == SQLITE_OK) rc = SQLITE_DONE;\n    int reset_rc',
     'ime_user_list(dict, "か", entries, 4) == 1'),
    ('no_candidate_dedup', 'learning', 'kernel/ime/ime_dict.c',
     '        if (i < count) continue;\n',
     '', 'count == expected'),
    ('truncated_spelling_compare', 'learning', 'kernel/ime/ime_dict.c',
     'if (kstrcmp(seen, kanji_text) == 0) break;',
     'if (kstrncmp(seen, kanji_text, 31) == 0) break;',
     'ime_dict_search(dict, readings[k], results, IME_MAX_RESULTS) == 3'),
    ('strict_spelling_boundary', 'learning', 'kernel/ime/ime_dict.c',
     'if (copy_len >= (int)sizeof(r->kanji)) {',
     'if (copy_len > (int)sizeof(r->kanji)) {',
     'results[0].pos_id == 0 && results[1].pos_id == 12 && results[2].pos_id == 13'),
]


def build(root, obj, exe):
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    host32.build([cc, '-m32', '-march=i386', '-nostartfiles', '-std=gnu11', '-Os',
                  '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                  '-Wall', '-Wextra', '-Werror', '-Werror=vla',
                  '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare',
                  '-Wno-type-limits', '-Wno-missing-field-initializers', '-D__cdecl=',
                  '-I' + str(ROOT),
                  *['-I' + str(ROOT / p) for p in
                    ('include', 'fs', 'drivers', 'sdk/include/os32', 'lib', 'lib/sqlite3', 'kernel', 'tools/tests')],
                  str(root / SOURCE), str(ROOT / 'tools/tests/fep_budget_linux.c'),
                  str(obj), '-lm', '-o', str(exe)], check=True)


def run(exe, case, runner):
    command = [str(exe), case]
    result = host32.run(command, runner=runner,
                        capture_output=True, text=True, timeout=120)
    print(result.stdout + result.stderr, end='', flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fep-standard-') as directory:
        tmp = Path(directory)
        obj = tmp / 'sqlite.o'
        cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
        host32.build([cc, '-m32', '-march=i386', '-std=gnu89', '-Os', '-DNDEBUG',
                      '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                      '-fsigned-char', '-fno-short-enums', '-include',
                      str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'), '-c',
                      str(ROOT / 'lib/sqlite3/sqlite3.c'), '-o', str(obj)], check=True)
        normal = tmp / 'normal'
        build(ROOT, obj, normal)
        for case in ('pressure', 'reserve', 'learning'):
            result = run(normal, case, args.runner)
            assert result.returncode == 0, result.returncode
        print('GREEN FEP APP+SYSTEM=319488 conversion; adjustable reserve/minimum; ext2 RW learning; prefix cap/order; full spelling boundary',
              flush=True)
        if args.mutate:
            for name, case, file, old, new, marker in MUTANTS:
                tree = tmp / name
                for source in FILES:
                    dest = tree / source
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(ROOT / source, dest)
                source = tree / file
                text = source.read_text()
                assert text.count(old) == 1, (name, old)
                source.write_text(text.replace(old, new))
                exe = tree / 'mutant'
                build(tree, obj, exe)  # Compile errors never count as RED.
                result = run(exe, case, args.runner)
                assert result.returncode == 1 and marker in result.stderr, (name, result)
                print(f'RED runtime {name}: {marker}', flush=True)


if __name__ == '__main__':
    main()
