"""Measure the PowerMac product ELFs supplied on the command line."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess

import lz4.block

from root_lz4_layout import PAGE, STAGING_BASE, STAGING_SIZE, round_page, segments


def measure(path, nm):
    data = path.read_bytes()
    loads = [segment for segment in segments(data) if segment[0] == 1]
    if len(loads) not in (1, 2):
        raise ValueError(f'{path}: expected one or two LOAD segments')
    _, offset, va, pa, filesz, memsz, _, _ = loads[0]
    symbols = {}
    for line in subprocess.check_output([nm, path], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    end = pa + memsz
    if va != pa or end != symbols['__kernel_resident_end']:
        raise ValueError(f'{path}: resident LOAD disagrees with symbols')
    chain = symbols['__kernel_chain_end']
    frame = packed = pages = 0
    if len(loads) == 2:
        _, start, va, pa, frame, size, flags, _ = loads[1]
        if not (va == pa == STAGING_BASE == symbols['__root_image_start']
                and frame == size and flags == 4 and chain <= pa):
            raise ValueError(f'{path}: unexpected root LOAD')
        record = symbols['__root_record_start'] - loads[0][2]
        if not 0 <= record <= filesz - 16:
            raise ValueError(f'{path}: root record outside resident LOAD')
        packed, length = struct.unpack_from('<II', data, offset + record + 8)
        raw = path.with_name('root.iso').read_bytes()
        if not (0 < packed <= frame <= STAGING_SIZE and length == len(raw)
                and not any(data[start+packed:start+frame])
                and lz4.block.decompress(data[start:start+packed], uncompressed_size=length) == raw):
            raise ValueError(f'{path}: compressed root disagrees with root.iso')
        pages = round_page(length) // PAGE
    return dict(elf=str(path), resident_end=end, chain_end=chain, frame=frame,
                packed=packed, iso_pages=pages, low_memsz=memsz, low_filesz=filesz)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', nargs='+', type=Path, help='product ELF with its adjacent root.iso if rooted')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    print(json.dumps([measure(path, cross / 'bin/powerpc-elf-nm') for path in args.elf], indent=2))


if __name__ == '__main__':
    main()
