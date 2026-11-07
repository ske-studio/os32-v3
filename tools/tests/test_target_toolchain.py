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
ROOT = Path(__file__).resolve().parents[2]


def check(root):
    x86 = query(root, config_only=True)
    assert x86['CC'] == ['i386-elf-gcc']
    assert x86['CFLAGS_MACHINE'][:2] == ['-m32', '-march=i386']
    assert query(root, ['CC=custom-cc'], config_only=True)['CC'] == ['custom-cc']
    ppc = query(root, ['ARCH=ppc', 'PLATFORM=powermac', 'CROSS_DIR=/x86'], config_only=True)
    assert ppc['CC'] == ['/home/hight/opt/cross-ppc/bin/powerpc-elf-gcc']
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
        shutil.copytree(ROOT / 'build', root / 'build', ignore=shutil.ignore_patterns('out', 'sdk', '__pycache__'))
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
