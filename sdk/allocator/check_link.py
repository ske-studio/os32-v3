#!/usr/bin/env python3
"""Opt-in f1b link gate, including --allow-multiple-definition links.

Inspect definitions in every selected input, not only the winning ELF symbol.
Public activation in sdk/link_guard.py is deferred to f's public switch.
"""
import re
from pathlib import Path
import subprocess

CONNECTED = {'malloc', 'free', 'calloc', 'realloc', '_malloc_r', '_free_r', '_calloc_r', '_realloc_r'}
MORECORE = {'sbrk', '_sbrk', '_sbrk_r'}
FORBIDDEN = {'malloc_trim', '_malloc_trim_r', 'memalign', 'aligned_alloc', 'valloc', 'pvalloc', 'mallopt', 'malloc_stats',
             'mallinfo', 'malloc_usable_size', 'cfree', '_memalign_r', '_valloc_r', '_pvalloc_r',
             '_mallopt_r', '_malloc_stats_r', '_mallinfo_r', '_malloc_usable_size_r', '_cfree_r',
             '__malloc_free_list', '__malloc_sbrk_start', '__malloc_current_mallinfo'}


def check(prefix, mapfile):
    source = Path(mapfile).read_text()
    selected = set(re.findall(r'(?m)^LOAD (.+)$', source))
    selected.update(re.findall(r'(?m)^([^\s]+\.a\([^\n)]+\))\s*(?:\n|\s)', source))
    definitions = {}
    for name in sorted(selected):
        match = re.fullmatch(r'(.+\.a)\((.+)\)', name)
        path = Path(match[1] if match else name)
        if not match and path.suffix != '.o':
            continue
        data = subprocess.check_output([str(prefix / 'bin/i386-elf-nm'), '-g', str(path)], text=True)
        member = None
        for line in data.splitlines():
            if line.endswith(':'):
                member = line[:-1]
                continue
            if match and member != match[2]:
                continue
            fields = line.split()
            if len(fields) == 3 and fields[1] in ('T', 'D', 'B', 'C', 'W', 'V'):
                definitions.setdefault(fields[2], []).append(name)
    for symbol in FORBIDDEN:
        if definitions.get(symbol):
            raise ValueError('unconnected nano entry/state: ' + symbol)
    for symbol in CONNECTED:
        providers = definitions.get(symbol, [])
        if len(providers) != 1 or not providers[0].endswith('(nano_adapter.o)'):
            raise ValueError('adapter provider/duplicate: ' + symbol)

    # This module is opt-in. At public activation f6 must forward CRT sbrk
    # through the adapter; an independent break owner cannot remain selected.
    for symbol in MORECORE:
        providers = definitions.get(symbol, [])
        if any(not provider.endswith('(nano_adapter.o)') for provider in providers):
            raise ValueError('independent break provider: ' + symbol)


def validate_output(prefix, mapfile, output):
    """A rejected opt-in link must not leave an executable artifact."""
    try:
        check(prefix, mapfile)
    except (ValueError, OSError, subprocess.CalledProcessError):
        Path(output).unlink(missing_ok=True)
        raise


if __name__ == '__main__':
    import argparse
    import os
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cross-dir', type=Path, default=Path(os.environ.get('CROSS_DIR', '/usr/local/cross')))
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        validate_output(args.cross_dir, args.map, args.output)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, 'nano link rejected: ' + str(exc) + '\n')
