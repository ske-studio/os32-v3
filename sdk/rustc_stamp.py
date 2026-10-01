#!/usr/bin/env python3
"""D35 rustc wrapper: check crate inputs before LTO; stamp every native output member.
Bitcode members retain a generation/hash record, since objcopy cannot edit bitcode.
"""
import hashlib
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
from os32_generations import (OS32X_HDR_VERSION, OS32_KAPI_ABI_GENERATION,
                              OS32_MEMORY_LAYOUT_GENERATION, OS32_SHLIB_PROTOCOL)

GEN = [OS32X_HDR_VERSION, OS32_KAPI_ABI_GENERATION, OS32_MEMORY_LAYOUT_GENERATION, OS32_SHLIB_PROTOCOL]


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    compiler, *args = sys.argv[1:]
    if '--out-dir' not in args or '--crate-name' not in args or '--emit' not in ' '.join(args):
        return subprocess.call([compiler, *args])
    for i, arg in enumerate(args[:-1]):
        if arg == '--extern' and '=' in args[i + 1]:
            dep = pathlib.Path(args[i + 1].split('=', 1)[1])
            if dep.exists():
                record = dep.with_name(dep.name + '.generations.json')
                if not record.exists():
                    raise SystemExit(f'rustc_stamp: missing crate generation record: {dep}')
                data = json.loads(record.read_text())
                if data['generations'] != GEN or data['sha256'] != digest(dep):
                    raise SystemExit(f'rustc_stamp: stale crate before LTO: {dep}')
    result = subprocess.run([compiler, *args], text=True, capture_output=True)
    if result.returncode:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        return result.returncode
    out = pathlib.Path(args[args.index('--out-dir') + 1])
    crate = args[args.index('--crate-name') + 1]
    suffix = next((a.split('=',1)[1] for a in args if a.startswith('extra-filename=')), '')
    for archive in list(out.glob('lib' + crate + suffix + '.rlib')) + list(out.glob('lib' + crate + suffix + '.a')):
        records = []
        with tempfile.TemporaryDirectory(prefix='os32-rust-') as td:
            tmp = pathlib.Path(td)
            stamp = tmp / 'note'
            stamp.write_bytes(struct.pack('<4I', *GEN))
            members = subprocess.check_output(['i386-elf-ar', 't', str(archive)], text=True).splitlines()
            for member in members:
                raw = subprocess.check_output(['i386-elf-ar', 'p', str(archive), member])
                obj = tmp / member
                obj.write_bytes(raw)
                if raw.startswith(b'\x7fELF'):
                    subprocess.run(['i386-elf-objcopy', '--remove-section=.os32_generations',
                                    '--add-section=.os32_generations=' + str(stamp),
                                    '--set-section-flags=.os32_generations=readonly', str(obj)], check=True)
                    subprocess.run(['i386-elf-ar', 'r', str(archive.resolve()), str(obj)], check=True,
                                   stdout=subprocess.DEVNULL)
                records.append({'member': member, 'sha256': digest(obj), 'native': raw.startswith(b'\x7fELF')})
        archive.with_name(archive.name + '.generations.json').write_text(json.dumps(
            {'generations': GEN, 'sha256': digest(archive), 'members': records}, indent=2) + '\n')
    for meta in out.glob('lib' + crate + suffix + '.rmeta'):
        meta.with_name(meta.name + '.generations.json').write_text(json.dumps(
            {'generations': GEN, 'sha256': digest(meta), 'members': []}) + '\n')
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
