#!/usr/bin/env python3
"""Validate every selected allocator provider, even losing duplicate inputs."""
import hashlib
import json
import re
from pathlib import Path
import subprocess

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from check_nano_inputs_target import TARGET, ABI
from check_link import CONNECTED, MORECORE, FORBIDDEN, selected_inputs


def check(prefix, mapfile, receipt):
    trusted = json.loads(Path(receipt).read_text()) if receipt else None
    if trusted and (trusted['target'] != TARGET or trusted['abi'] != ABI):
        raise ValueError('receipt target/ABI differs')
    definitions, hashes, symbols = {}, {}, {}
    for name in sorted(selected_inputs(mapfile)):
        match = re.fullmatch(r'(.+\.a)\((.+)\)', name)
        path = Path(match[1] if match else name)
        if not match and path.suffix != '.o':
            continue
        if path not in symbols:
            data = subprocess.check_output([str(prefix / 'bin/powerpc-elf-nm'), '-g', str(path)], text=True)
            member = None
            table = {}
            for line in data.splitlines():
                if line.endswith(':'):
                    member = line[:-1]
                    continue
                fields = line.split()
                if len(fields) == 3 and fields[1] in ('T', 'D', 'B', 'C', 'W', 'V', 'R', 'A'):
                    table.setdefault(member, []).append(fields[2])
            symbols[path] = table
        body = (subprocess.check_output([str(prefix / 'bin/powerpc-elf-ar'), 'p', str(path), match[2]])
                if match else path.read_bytes())
        hashes[name] = hashlib.sha256(body).hexdigest()
        for symbol in symbols[path].get(match[2] if match else None, []):
            definitions.setdefault(symbol, []).append(name)
    for symbol in FORBIDDEN:
        if definitions.get(symbol):
            raise ValueError('unconnected nano entry/state: ' + symbol)
    if trusted is None:
        if any(symbol in definitions for symbol in CONNECTED | MORECORE) or any(
                symbol.startswith('os32_private_') for symbol in definitions):
            raise ValueError('USER allocator input requires libos32nano receipt')
        # Freestanding ELF fixtures/apps without any allocator need no archive.
        return set()
    for symbol in CONNECTED:
        providers = definitions.get(symbol, [])
        if len(providers) != 1 or hashes[providers[0]] != trusted['adapter']:
            raise ValueError('adapter provider/duplicate: ' + symbol)
    for symbol in MORECORE:
        providers = definitions.get(symbol, [])
        expected = (trusted['crt'] if symbol in ('sbrk', '_sbrk') else
                    trusted['sbrkr'] if symbol == '_sbrk_r' else trusted['adapter'])
        required = symbol in ('os32_nano_morecore', 'os32_nano_sbrk') or (
            trusted['crt'] and symbol in ('sbrk', '_sbrk', 'os32_nano_crt_sbrk'))
        if len(providers) > 1 or (required and not providers) or any(hashes[p] != expected for p in providers):
            raise ValueError('independent break provider: ' + symbol)
    # Private nano state/entries must come from the validated transformed members.
    for symbol, providers in definitions.items():
        if symbol.startswith('os32_private_'):
            if len(providers) != 1 or hashes[providers[0]] not in trusted['nano'].values():
                raise ValueError('private nano provider/duplicate: ' + symbol)
    return {name for name, digest in hashes.items() if digest in trusted['nano'].values()}


def validate_output(prefix, mapfile, output, receipt):
    """A rejected link must not leave an executable artifact."""
    try:
        return check(prefix, mapfile, receipt)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError):
        Path(output).unlink(missing_ok=True)
        raise


if __name__ == '__main__':
    import argparse
    import os
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cross-dir', type=Path, default=Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')))
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--receipt', type=Path, required=True)
    args = parser.parse_args()
    try:
        validate_output(args.cross_dir, args.map, args.output, args.receipt)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, 'nano link rejected: ' + str(exc) + '\n')
