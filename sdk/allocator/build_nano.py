#!/usr/bin/env python3
"""Build validated private nano members and adapter; never mutate libc.a."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import check_nano_inputs as inputs


def build(prefix, output, adapter=None, reuse=None, crt=None):
    ledger = json.loads(inputs.LEDGER.read_text())
    if reuse is None:
        inputs.check(prefix, ledger)
        inputs.sdk_inputs(ledger)
    output.mkdir(parents=True, exist_ok=True)
    spec = ledger['sdk_build']
    objects = []
    for member in ([] if reuse else spec['members']):
        obj = output / member
        obj.write_bytes(inputs.command(prefix, 'ar', 'p', prefix / 'i386-elf/lib/libc.a', member))
        flags = ['--redefine-sym=' + a + '=' + b for a, b in spec['renames'].items()]
        inputs.command(prefix, 'objcopy', *flags, obj)
        objects.append(obj)
    obj = output / 'nano_adapter.o'
    inputs.command(prefix, 'gcc', '-std=gnu11', '-Os', '-ffreestanding', '-fno-builtin',
                   '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-march=i386',
                   '-fno-pie', '-fno-stack-protector',
                   '-I' + str(ROOT / 'sdk/allocator'), '-I' + str(ROOT / 'sdk/include/os32'),
                   *(['-include', str(ROOT / 'sdk/include/os32/os32_unit_stamp.h')] if crt else ['-DOS32_NANO_FIXTURE']),
                   '-c', adapter or ROOT / 'sdk/allocator/nano_adapter.c', '-o', obj)
    if reuse is not None:
        # Reuse the validated private nano members, replacing only the adapter.
        for member in spec['members']:
            target = output / member
            target.write_bytes(inputs.command(prefix, 'ar', 'p', reuse, member))
            objects.append(target)
    objects.append(obj)
    archive = output / ('libos32nano.a' if crt else 'libos32nano_fixture.a')
    archive.unlink(missing_ok=True)
    inputs.command(prefix, 'ar', 'rcs', archive, *objects)
    digest = lambda data: hashlib.sha256(data).hexdigest()
    receipt = {'adapter': digest(obj.read_bytes()),
               'nano': {member: digest((output / member).read_bytes()) for member in spec['members']},
               'crt': digest(crt.read_bytes()) if crt else None,
               'sbrkr': digest(inputs.command(prefix, 'ar', 'p', prefix / 'i386-elf/lib/libc.a', 'libc_a-sbrkr.o'))}
    archive.with_suffix('.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cross-dir', type=Path, default=Path(os.environ.get('CROSS_DIR', '/usr/local/cross')))
    parser.add_argument('output', type=Path)
    parser.add_argument('--crt', type=Path)
    args = parser.parse_args()
    print(build(args.cross_dir, args.output, crt=args.crt))
