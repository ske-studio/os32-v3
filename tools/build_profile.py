#!/usr/bin/env python3
"""Ordered make expansion and actual compiler commands, including old trees."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

VARIABLES = '''ARCH PLATFORM CC AR AS LD OBJCOPY READELF CFLAGS_MACHINE CFLAGS_BASE
CFLAGS_SQLITE C_KERNEL ASM_KERNEL ASM_EXTRA_OBJ GAS_KERNEL KERNEL_SRC_DIRS
INC_KERNEL INC_DRIVERS INC_GFX INC_FS INC_FATFS INC_EXEC INC_KAPI INC_LIB
C_KERNEL_OBJ ASM_KERNEL_OBJ GAS_KERNEL_OBJ R1_SMALL R1_OBJECTS
CORE_KERNEL_INCLUDES KERNEL_PAIRED_MODULES'''.split()


def query(root, assignments=(), config_only=False, commands=False):
    root = Path(root).resolve()
    env = dict(os.environ)
    for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'GNUMAKEFLAGS'):
        env.pop(name, None)
    with tempfile.TemporaryDirectory(prefix='build-profile-') as directory:
        temporary = Path(directory)
        query_mk = temporary / 'query.mk'
        result = temporary / 'values'
        query_mk.write_text('.PHONY: __profile\n__profile:\n' + ''.join(
            f'\t$(file >>{result},{name}=$({name}))\n' for name in VARIABLES) + '\t@:\n')
        base = ['make', '--no-print-directory', '-s', '-f',
                'build/config.mk' if config_only else 'Makefile', '-f', str(query_mk),
                'BUILD_OUT=' + str(temporary / 'out'), *assignments]
        subprocess.run([*base, '__profile'], cwd=root, env=env, check=True,
                       capture_output=True, text=True)
        values = dict(line.split('=', 1) for line in result.read_text().splitlines())
        profile = {name: shlex.split(value) for name, value in values.items()}
        if commands:
            dry = subprocess.run([*base, '-n', *profile['C_KERNEL_OBJ'],
                                  *profile['ASM_KERNEL_OBJ'], *profile['GAS_KERNEL_OBJ']],
                                 cwd=root, env=env, capture_output=True, text=True, check=True)
            cc = profile['CC']
            profile['argv'] = [argv for line in dry.stdout.splitlines()
                               if (argv := shlex.split(line))[:len(cc)] == cc and '-c' in argv]
        return profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--config-only', action='store_true')
    parser.add_argument('--commands', action='store_true')
    parser.add_argument('assignments', nargs='*')
    args = parser.parse_args()
    print(json.dumps(query(args.root, args.assignments, args.config_only, args.commands), indent=2))


if __name__ == '__main__':
    main()
