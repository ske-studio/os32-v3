#!/usr/bin/env python3
"""T4c-3: app pressure at the product ILP32 cap and standard ext2 RW learning."""
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import sqlite3
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'tools/tests/fep_standard_host.c'
# Only changed implementation sources need overlay copies; other includes
# fall back to ROOT. All regressions run normally before any mutant.
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
    ('no_candidate_dedup', 'learning', 'kernel/ime_dict.c',
     '        if (i < count) continue;\n',
     '', 'count == expected'),
]


def dictionary(tmp):
    # Build the shipped M dictionary from bundled IPADIC, using the real tool.
    # Seed the old schema (no spelling-first index) with 32 distinct learned
    # spellings at the input prefix, as produced by ime_dict_learn.
    spec = importlib.util.spec_from_file_location('fep_builder', ROOT / 'tools/fep_to_sqlite.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    entries = builder.normalize_costs(builder.load_ipadic(str(ROOT / 'assets/ipadic')))
    path = tmp / 'fep.db'
    builder.create_db(entries, str(path), builder.SIZE_DEFS['M']['cost_limit'])
    with sqlite3.connect(path) as db:
        rows = db.execute("SELECT kanji FROM dict WHERE yomi >= ? AND yomi < ?",
                          ('しょう', 'しょう\uffff')).fetchall()
        assert len(rows) >= 700, len(rows)
        words = sorted({row[0] for row in rows})[:32]
        assert len(words) == 32
        db.executescript('CREATE TABLE dict_user(yomi TEXT NOT NULL,kanji TEXT NOT NULL,'
                         'freq INTEGER DEFAULT 1,last_ts INTEGER DEFAULT 0,PRIMARY KEY(yomi,kanji));'
                         'CREATE INDEX idx_user_yomi ON dict_user(yomi);')
        db.executemany('INSERT INTO dict_user VALUES(?,?,?,?)',
                       [('しょう', word, i + 1, i) for i, word in enumerate(words)])
    print(f'INPUT real bundled IPADIC M shou_candidates={len(rows)} learned=32 legacy_schema',
          flush=True)
    return path


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


def run(exe, case, runner, data):
    command = [str(exe), case]
    if case == 'prefix':
        command.append(str(data))
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
        data = dictionary(tmp)
        obj = tmp / 'sqlite.o'
        cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
        host32.build([cc, '-m32', '-march=i386', '-std=gnu89', '-Os', '-DNDEBUG',
                      '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                      '-fsigned-char', '-fno-short-enums', '-include',
                      str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'), '-c',
                      str(ROOT / 'lib/sqlite3/sqlite3.c'), '-o', str(obj)], check=True)
        normal = tmp / 'normal'
        build(ROOT, obj, normal)
        for case in ('pressure', 'learning', 'prefix'):
            result = run(normal, case, args.runner, data)
            assert result.returncode == 0, result.returncode
        print('GREEN FEP APP+SYSTEM=319488 conversion; ext2 RW learning; prefix cap/order/VM steps',
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
                result = run(exe, case, args.runner, data)
                assert result.returncode == 1 and marker in result.stderr, (name, result)
                print(f'RED runtime {name}: {marker}', flush=True)


if __name__ == '__main__':
    main()
