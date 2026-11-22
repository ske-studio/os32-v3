#!/usr/bin/env python3
"""Observe the resident shell window in the detached product kernel."""
import argparse
import hashlib
import os
import re
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
from kboot_accept import build, mutant_settings
from kboot_layout import report_product
from link_sh_run import Session
from ppc_shm import guest_available
from root_lz4_layout import STAGING_BASE, segments


def verify_handoff(lines, resident_end, frame_end):
    def fields(pattern):
        match, = [m for line in lines if (m := re.fullmatch(pattern, line))]
        return match.groups()

    end, gap, regs, avail = map(int, fields(
        r'KBOOT OF-IMAGE end=(\d+) reserved=(\d+) reg=(\d+) available=(\d+)'))
    ranges = {}
    for name, count in [('reg', regs), ('available', avail)]:
        intervals = [tuple(map(int, line.split()[3:])) for line in lines
                     if line.startswith('KBOOT OF-RANGE ' + name + ' ')]
        assert len(intervals) == count and count > 0, name
        previous = 0
        for first, stop in intervals:
            assert previous <= first < stop, name
            previous = stop
        ranges[name] = intervals

    def covered(name, first, stop):
        for start, end in ranges[name]:
            if start > first:
                break
            first = max(first, end)
            if first >= stop:
                return True
        return False

    assert end == resident_end and covered('reg', 0x100000, 0xf00000)
    assert all(b <= 0x100000 or a >= end for a, b in ranges['available'])
    # OF reports the loaded frame as claimed. Its pages return to the pool
    # after expansion, while this snapshot still describes the handoff.
    assert gap == 0 and covered('available', end, STAGING_BASE)
    assert all(b <= STAGING_BASE or a >= frame_end for a, b in ranges['available'])
    assert covered('available', frame_end, 0xf00000)
    detach, = [i for i, line in enumerate(lines) if re.fullmatch(r'KBOOT OF-OFF calls=\d+', line)]
    mmu, = [i for i, line in enumerate(lines) if line.startswith('KBOOT MMU-D ')]
    assert detach < mmu, 'OF detach must precede the MMU observation'
    pa, size = fields(r'KBOOT MMU-D root=0x[0-9a-fA-F]+ hpt=0x([0-9a-fA-F]+) bytes=(\d+) ram_bat=\d+')
    hpt, size = int(pa, 16), int(size)
    assert size == 0x20000 and hpt % size == 0 and 0x1000000 <= hpt <= 0x2000000 - size
    assert all(covered(name, hpt, hpt + size) for name in ranges)


def observe(elf, cpu, folder, expected, resident_end, frame_end):
    session = Session(elf, cpu, folder, 90)
    serial = folder / 'serial-a.log'
    try:
        session.start()
        def lines():
            text = serial.read_text(errors='replace') if serial.exists() else ''
            return text[:text.rfind('\n')+1].splitlines()
        marker = 'KBOOT SHELL-LAYOUT ' + expected
        session.wait(lambda: marker in lines(), marker, 60)
        output = lines()
        index = output.index(marker)
        verify_handoff(output[:index], resident_end, frame_end)
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
    product = build(work / 'product', '-j4', 'IMAGE=kboot-link-sh', 'kernel')
    report_product(product)
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
        resident_end = report_product(elf)
        data = elf.read_bytes()
        frame, = [p for p in segments(data) if p[0] == 1 and p[3] == STAGING_BASE]
        frame_end = STAGING_BASE + frame[5]
        digest = hashlib.sha256(data).hexdigest()
        if digest in hashes:
            raise RuntimeError(f'duplicate image {elf}')
        hashes.add(digest)
        print(f'{name} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, expected, resident_end, frame_end)
                print(f'{"RED" if change else "GREEN"} {cpu} {name} {expected}')
    print(f'PASS distinct images={len(hashes)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
