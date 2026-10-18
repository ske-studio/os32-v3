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
# fall back to ROOT. Both regressions run normally before any mutant.
FILES = [SOURCE, 'lib/sqlite3/os32_sqlite_vfs.c', 'kernel/ime_dict.c']
MUTANTS = [
    ('fep_uses_app_cap', 'pressure', 'lib/sqlite3/os32_sqlite_vfs.c',
     'if (budget->context == BUDGET_APP &&',
     'if ((budget->context == BUDGET_APP || budget->context == BUDGET_FEP) &&',
     'ime_dict_search(dict, "か", result, IME_MAX_RESULTS) == 2'),
    ('skip_learning', 'learning', 'kernel/ime_dict.c',
     'rc = sqlite3_step(stmt);\n    if (rc != SQLITE_DONE && is_ioerr(rc)',
     'rc = SQLITE_DONE;\n    if (rc != SQLITE_DONE && is_ioerr(rc)',
     'ime_user_list(dict, "か", entries, 4) == 1'),
    ('exact_dictionary_duplicate', 'learning', 'kernel/ime_dict.c',
     '    "    AND NOT EXISTS (SELECT 1 FROM dict_user u"\n'
     '    "      WHERE u.yomi = ?1 AND u.kanji = d.kanji)"\n',
     '', 'count == expected'),
    ('prefix_dictionary_duplicate', 'learning', 'kernel/ime_dict.c',
     '    "    AND NOT EXISTS (SELECT 1 FROM dict_user u WHERE u.kanji = d.kanji"\n'
     '    "      AND u.yomi >= ?1 AND u.yomi < ?1 || X\'EFBFBF\')"\n',
     '', 'count == expected'),
    ('prefix_user_duplicate', 'learning', 'kernel/ime_dict.c',
     '    "  SELECT kanji, 0 as pos_id, MIN(-10000 - freq) as cost"\n'
     '    "    FROM dict_user WHERE yomi >= ?1 AND yomi < ?1 || X\'EFBFBF\'"\n'
     '    "    GROUP BY kanji"\n',
     '    "  SELECT kanji, 0 as pos_id, (-10000 - freq) as cost"\n'
     '    "    FROM dict_user WHERE yomi >= ?1 AND yomi < ?1 || X\'EFBFBF\'"\n',
     'count == expected'),
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
    result = host32.run([str(exe), case], runner=runner,
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
        for case in ('pressure', 'learning'):
            result = run(normal, case, args.runner)
            assert result.returncode == 0, result.returncode
        print('GREEN FEP APP+SYSTEM=319488 conversion; ext2 RW learning', flush=True)
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
