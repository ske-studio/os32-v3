#!/usr/bin/env python3
"""Toolchain selection and inherited command-line overrides without compilers."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_profile import query
from build_fixture import copy_build_tree
ROOT = Path(__file__).resolve().parents[2]


def check(root):
    for cache in ('', '/fixture/ccache'):
        assignments = ['CCACHE=' + cache]
        prefix = ([cache, 'compiler_check=content', 'base_dir=', 'sloppiness=',
                   'hash_dir=true'] if cache else [])
        x86 = query(root, assignments, config_only=True)
        assert x86['RAW_CC'] == ['i386-elf-gcc']
        assert x86['CC'] == prefix + x86['RAW_CC']
        assert x86['CFLAGS_MACHINE'][:2] == ['-m32', '-march=i386']
        custom = query(root, [*assignments, 'CC=custom-cc'], config_only=True)
        assert custom['CC'] == custom['RAW_CC'] == ['custom-cc']
        ppc = query(root, [*assignments, 'ARCH=ppc', 'PLATFORM=powermac',
                           'CROSS_DIR=/x86'], config_only=True)
        assert ppc['RAW_CC'] == ['/home/hight/opt/cross-ppc/bin/powerpc-elf-gcc']
        assert ppc['CC'] == prefix + ppc['RAW_CC']
        assert '-mbig-endian' in ppc['CFLAGS_MACHINE']
        assert '-march=i386' not in ppc['CFLAGS_MACHINE']
    try:
        query(root, ['ARCH=unknown'], config_only=True)
    except subprocess.CalledProcessError as error:
        assert 'unsupported ARCH' in error.stderr
    else:
        raise AssertionError('unknown architecture accepted')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        copy_build_tree(root)
        check(root)
        if args.mutate:
            profile = root / 'build/arch/ppc.mk'
            source = profile.read_text()
            anchor = '-mbig-endian'
            assert source.count(anchor) == 1
            profile.write_text(source.replace(anchor, '-mlittle-endian'))
            try:
                check(root)
            except AssertionError:
                print('RED: PPC endianness mutation rejected at runtime')
            else:
                raise SystemExit('mutation survived')
    print('PASS target toolchain')
