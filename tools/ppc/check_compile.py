#!/usr/bin/env python3
"""NOTEST: compile selected PowerPC units in a fresh isolated source snapshot."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_compare import snapshot
from build_profile import query
from ppc.check_accept import forbidden_dependencies, validate
ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    data = path.read_bytes()
    if data[:4] != b'\x7fELF' or len(data) < 20:
        raise ValueError('not an ELF object')
    endian = 'big' if data[5] == 2 else 'little'
    return dict(elf_class=data[4], endian=endian,
                machine=int.from_bytes(data[18:20], endian))



def classify(stderr):
    if 'No such file or directory' in stderr:
        if any(name in stderr for name in ('arch_io.h', 'arch_cpu.h', 'platform_io.h')):
            return 'missing-architecture-header'
        return 'missing-header'
    if any(word in stderr for word in ('assembler', 'register name', 'operand', 'instruction')):
        return 'architecture-instruction'
    return 'compile-error'


def measure(root, assignments, source_profile='selected'):
    if source_profile == 'legacy-x86':
        legacy = query(root, ['ARCH=x86', 'PLATFORM=pc98', 'KSTRING_C_SRC=lib/kstring_c.c'], config_only=True)
        assignments = [*assignments, 'C_KERNEL=' + ' '.join(legacy['C_KERNEL']),
                       'KERNEL_SRC_DIRS=' + ' '.join(legacy['KERNEL_SRC_DIRS'])]
    profile = query(root, assignments, commands=True)
    declared = profile['CC']
    if len(declared) != 1 or Path(declared[0]).name != 'powerpc-elf-gcc':
        raise ValueError('selected compiler is not powerpc-elf-gcc')
    report = dict(target='ppc-powermac', source_profile=source_profile, cc=declared,
                  selected=profile['C_KERNEL'] + profile['GAS_KERNEL'], units=[])
    for argv in profile['argv']:
        argv = [*argv, '-D__cdecl=']
        source = argv[argv.index('-c') + 1]
        output = Path(argv[argv.index('-o') + 1])
        if not output.is_absolute():
            output = root / output
        output.parent.mkdir(parents=True, exist_ok=True)
        result = subprocess.run(argv, cwd=root, text=True, capture_output=True)
        row = dict(source=source, argv=argv, returncode=result.returncode,
                   status='failed' if result.returncode else 'success',
                   classification=classify(result.stderr) if result.returncode else 'success',
                   error=result.stderr, first_error=next((line for line in result.stderr.splitlines()
                                                        if 'error:' in line), ''))
        dep = output.with_suffix('.d')
        row['dependencies'] = dep.read_text() if dep.is_file() else None
        if result.returncode == 0:
            row['elf'] = identity(output)
            row['forbidden_dependencies'] = forbidden_dependencies(row['dependencies'] or '')
        report['units'].append(row)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-profile', choices=['selected', 'legacy-x86'], default='selected')
    parser.add_argument('--accept', action='store_true')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.accept and args.source_profile != 'selected':
        parser.error('--accept requires --source-profile selected')
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file():
        print('SKIP: PowerPC compiler is unavailable', file=sys.stderr)
        return 1 if args.accept else 0
    with tempfile.TemporaryDirectory(prefix='ppc-selected-') as directory:
        tree = Path(directory) / 'tree'
        snapshot(ROOT, 'HEAD', tree)
        report = measure(tree, ['ARCH=ppc', 'PLATFORM=powermac', 'PPC_CROSS_DIR=' + str(cross)], args.source_profile)
    errors = validate(report) if args.source_profile == 'selected' else []
    report['accept_errors'] = errors
    text = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    if args.json:
        print(text, end='')
    else:
        print(f"{sum(r['status'] == 'success' for r in report['units'])}/{len(report['units'])} compiled; {len(errors)} acceptance errors")
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
