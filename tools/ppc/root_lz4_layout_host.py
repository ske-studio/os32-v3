#!/usr/bin/env python3
"""PPC_BOOT_DESIGN §§6–7 and root-LZ4 stage 1: independent boundary cases
for the image envelope, p_memsz, and the pre/post OF reservation verdict.
"""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

from root_lz4_layout import PAGE, STAGING_BASE, STAGING_SIZE
from root_lz4_inventory import COLUMNS, check_staging_size, envelope, image_table, measure
from root_lz4_layout_run import LOW, check_before, verify


def fixture(folder, high):
    # A rootless OF client with one file-backed word and one extra BSS page.
    data = bytearray(0x1004)
    data[:6] = b'\x7fELF\x01\x02'
    struct.pack_into('>I', data, 28, 52)
    struct.pack_into('>HH', data, 42, 32, 1)
    struct.pack_into('>8I', data, 52, 1, 4096, LOW, LOW, 4, 8192, 7, 4096)
    elf = folder/'fixture.elf'
    elf.write_bytes(data)
    row, _ = measure(elf, fixture=True)
    if row['resident_end'] != LOW + 8192:
        raise ValueError('BSS omitted from resident size')
    rows = [dict(zip(COLUMNS, fields)) for fields in
            [(LOW+8192, LOW+12288, 16384, 15000, 7), (LOW+4096, LOW+8192, 32768, 30000, 3)]]
    for name, row in zip(('largest-low', 'largest-frame'), rows):
        row['names'] = [name]
    rows = image_table(rows)
    if {r['names'][0] for r in rows} != {'largest-low', 'largest-frame'}:
        raise ValueError('largest frame image omitted')
    if tuple(envelope(rows).values()) != (LOW+8192, LOW+12288, 32768, 30000, 7):
        raise ValueError('largest frame image omitted')

    check_staging_size(STAGING_SIZE, STAGING_SIZE)
    try:
        check_staging_size(STAGING_SIZE + 1, STAGING_SIZE + PAGE)
    except ValueError as error:
        if str(error) != 'compressed root does not fit boot staging area':
            raise
    else:
        raise ValueError('oversized compressed root accepted')

    lo = (1, 0, LOW, LOW, 4, 8192, 7, 4096)
    hi = (1, 4, high, high, 4, 4096, 4, 4096)
    ram = 32 << 20
    before = {'entry':LOW, 'real-base':0xffffffff, 'load-base':16<<20, 'mmu':42,
              'reg':[(0, ram)], 'available':[(0,ram)]}
    after = {'ENTRY':[(LOW,1)], 'MMU':[(42,1)], 'LOAD-BASE':[(16<<20,0)],
             'REAL-BASE':[(0xffffffff,0)], 'RAM':[(0,ram)],
             'AVAILABLE':[(0,LOW),(LOW+8192,high-LOW-8192),(high+4096,ram-high-4096)],
             'TRANSLATED':[(LOW,LOW+8192),(high,high+4096)],
             'CRC':[(LOW,zlib.crc32(b'abcd')),(high,zlib.crc32(b'efgh'))],
             'LAYOUT-END':[(1,0)]}
    verify(before,after,b'abcdefgh',[lo,hi])
    small = dict(before, reg=[(0, high)], available=[(0, high)])
    try:
        check_before(small, [lo, hi], 8)
    except ValueError as error:
        if str(error) != f'pre-existing reservation or non-RAM at {high:#x}':
            raise
    else:
        raise ValueError('out-of-RAM LOAD accepted')
    for index in (1, 2):
        ranges = after['AVAILABLE'].copy()
        start, size = ranges[index]
        ranges[index] = (start + 4096, size - 4096)
        try:
            verify(before, dict(after, AVAILABLE=ranges), b'abcdefgh', [lo, hi])
        except ValueError as error:
            if str(error) != 'heap/chain/15 MiB window':
                raise
        else:
            raise ValueError('unexpected reservation in pool accepted')
    after['AVAILABLE'][2] = (high,ram-high)
    try:
        verify(before,after,b'abcdefgh',[lo,hi])
    except ValueError as error:
        if str(error) != f'LOAD still available {high:#x}':
            raise
    else:
        raise ValueError('unreserved high LOAD accepted')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        fixture(folder, STAGING_BASE)
        if args.mutate:
            mutations = [
                ('root_lz4_inventory.py', 'packed <= frame <= STAGING_SIZE',
                 'packed <= frame', 'oversized compressed root accepted'),
                ('root_lz4_inventory.py', "return sorted(rows, key=lambda row: (row['frame'], row['names'][0]))",
                 "return sorted(rows, key=lambda row: (row['frame'], row['names'][0]))[:-1]",
                 'largest frame image omitted'),
                ('root_lz4_inventory.py', 'end = pa + memsz', 'end = pa + loads[0][4]',
                 'BSS omitted from resident size'),
                ('root_lz4_layout_run.py', 'require(not overlap(avail, pa, pa+mem)',
                 'require(p == loads[1] or not overlap(avail, pa, pa+mem)', 'unreserved high LOAD accepted')]
            for filename, old, new, reason in mutations:
                for p in ('root_lz4_layout_host.py','root_lz4_inventory.py','root_lz4_layout_run.py'):
                    shutil.copy(here/p, folder/p)
                target = folder/filename
                source = target.read_text()
                if source.count(old) != 1:
                    raise ValueError('mutation anchor '+old)
                target.write_text(source.replace(old,new))
                run = subprocess.run([sys.executable,'-B',folder/Path(__file__).name],
                                     env=dict(os.environ,PYTHONPATH=str(here)),
                                     capture_output=True, text=True)
                if run.returncode == 0 or run.stderr.splitlines()[-1] != 'ValueError: '+reason:
                    raise RuntimeError(run.stderr)
                print('RED', reason)
    print('PASS root LZ4 layout host')


if __name__ == '__main__':
    main()
