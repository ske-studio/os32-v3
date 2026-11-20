#!/usr/bin/env python3
"""Observe the resident shell window in the detached product kernel."""
import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
from kboot_accept import build, mutant_settings
from link_sh_run import Session
from ppc_shm import guest_available


def observe(elf, cpu, folder, expected):
    session = Session(elf, cpu, folder, 90)
    serial = folder / 'serial-a.log'
    try:
        session.start()
        def lines():
            return serial.read_text(errors='replace').splitlines() if serial.exists() else []
        session.wait(lambda: any(line.startswith('KBOOT SHELL-LAYOUT') for line in lines()),
                     'shell layout', 60)
        output = lines()
        index = output.index('KBOOT SHELL-LAYOUT ' + expected)
        if not any(line.startswith('KBOOT OF-OFF calls=') for line in output[:index]):
            raise RuntimeError('missing preceding detach milestone')
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    if not args.build_only and not guest_available():
        return 3
    work = Path(tempfile.mkdtemp(prefix='shell-layout-', dir=os.environ.get('TMPDIR')))
    normal = 'image=181 heap=64 guard=1 stack=10 available=256'
    cases = [('normal', None, normal)]
    if args.mutate:
        cases += [('guard-present', 'MEM_SHELL_GUARD, MEM_SHELL_GUARD | PAGE_RW',
                   normal.replace('guard=1', 'guard=0')),
                  ('heap-alias', 'MEM_SHELL_HEAP_BASE, MEM_SHELL_LOAD_ADDR | PAGE_RW',
                   normal.replace('heap=64', 'heap=63'))]
    hashes = set()
    for name, change, expected in cases:
        folder = work / name
        folder.mkdir()
        settings = []
        if change:
            anchor = '    shell_layout_report(info);'
            settings = mutant_settings(folder, name, 'platform/powermac/ppc_kernel.c',
                                       anchor, f'    paging_pte_restore({change});\n' + anchor)
        elf = build(folder / 'kernel', '-j4', 'IMAGE=kboot-link-sh',
                    'KBOOT_SHELL_LAYOUT_DIAG=1', *settings, 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        if digest in hashes:
            raise RuntimeError(f'duplicate image {elf}')
        hashes.add(digest)
        print(f'{name} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, expected)
                print(f'{"RED" if change else "GREEN"} {cpu} {name} {expected}')
    print(f'PASS distinct images={len(hashes)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
