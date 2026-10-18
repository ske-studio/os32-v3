#!/usr/bin/env python3
"""T4c-3 CD FEP: ILP32, product MEMSYS5 and real RO filesystem path."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'tools/tests/fep_ro_host.c'
# Only implementation sources need overlay copies; unchanged headers resolve
# through ROOT. Relative includes must also resolve inside each mutant tree.
FILES = [SOURCE, 'tools/tests/sqlite_cd_host.c', 'tools/tests/cd_read_host.c',
         'fs/vfs.c', 'fs/vfs_fd.c', 'fs/iso9660.c', 'drivers/atapi.c',
         'lib/sqlite3/os32_sqlite_vfs.c', 'kernel/ime_dict.c']
MUTANTS = [
    ('ro_create', '} else {\n        /* 学習用テーブルの作成',
     '} if (1) {\n        /* 学習用テーブルの作成', 'create_attempts == 0'),
    ('ro_learn_prepare', 'if (!dict->readonly) {', 'if (1) {', 'learn_prepares == 0'),
]


def build(root, obj, exe):
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    host32.build([cc, '-m32', '-march=i386', '-nostartfiles', '-std=gnu11', '-Os',
                  '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                  '-Wall', '-Wextra', '-Werror', '-Werror=vla',
                  '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare',
                  '-Wno-type-limits',
                  '-Wno-missing-field-initializers', '-D__cdecl=',
                  '-I' + str(ROOT / 'tools/tests/atapi_hostshim'),
                  '-I' + str(ROOT),
                  *['-I' + str(ROOT / p) for p in
                    ('include', 'fs', 'drivers', 'sdk/include/os32', 'lib', 'lib/sqlite3', 'kernel')],
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
    with tempfile.TemporaryDirectory(prefix='fep-ro-') as directory:
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
        for case in ('user', 'no_user'):
            result = run(normal, case, args.runner)
            assert result.returncode == 0, result.returncode
        print('GREEN CD FEP dict_user present/absent; APP+SYSTEM=319488', flush=True)
        if args.mutate:
            for name, old, new, marker in MUTANTS:
                tree = tmp / name
                for file in FILES:
                    dest = tree / file
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(ROOT / file, dest)
                source = tree / 'kernel/ime_dict.c'
                text = source.read_text()
                assert text.count(old) == 1, (name, old)
                source.write_text(text.replace(old, new))
                exe = tree / 'mutant'
                build(tree, obj, exe)  # Compile errors never count as RED.
                result = run(exe, 'user', args.runner)
                assert result.returncode == 1 and marker in result.stderr, (name, result)
                print(f'RED runtime {name}: {marker}', flush=True)


if __name__ == '__main__':
    main()
