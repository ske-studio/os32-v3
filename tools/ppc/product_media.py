"""Build the installable classic HFS partition from the product ELF pair."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from root_lz4_layout import segments


def product_load(data):
    loads = [p for p in segments(data) if p[0] == 1]
    if len(loads) != 1 or loads[0][2] != 0x100000:
        raise ValueError("product must have one kernel LOAD")


def partition(image):
    if image[:4] != b'ER\x02\x00':
        raise ValueError("expected 512-byte Apple disk")
    count, = struct.unpack_from('>I', image, 516)
    matches = []
    for index in range(1, count + 1):
        entry = image[512*index:512*(index+1)]
        if entry[:2] != b'PM':
            raise ValueError("invalid partition map")
        if entry[48:80].rstrip(b'\0') == b'Apple_HFS':
            start, sectors = struct.unpack_from('>II', entry, 8)
            volume = image[start*512:(start+sectors)*512]
            if len(volume) != sectors*512 or volume[1024:1026] != b'BD':
                raise ValueError("invalid classic HFS partition")
            blocks, block_size = struct.unpack_from('>HI', volume, 1042)
            first, = struct.unpack_from('>H', volume, 1052)
            if block_size < 512 or block_size % 512 or first*512 + blocks*block_size != len(volume):
                raise ValueError("HFS allocation geometry differs from partition length")
            matches.append(volume)
    if len(matches) != 1:
        raise ValueError("expected one HFS partition")
    return matches[0]


def hdd_volume(hybrid):
    # Apple, Inside Macintosh: Files, ch. 2, "Master Directory Block":
    # the alternate MDB occupies the next-to-last 512-byte logical block.
    # Relocate the allocation area as a unit: all catalog and file extents
    # are relative to drAlBlSt. Reserve enough bitmap sectors for the added
    # free blocks; both MDBs describe the complete fixed-size partition.
    sectors = 8 * 2048  # PM_HFS_SECTORS
    bitmap, _, old_blocks, block_size = struct.unpack_from('>HHHI', hybrid, 1024+14)
    old_first, = struct.unpack_from('>H', hybrid, 1024+28)
    old_free, = struct.unpack_from('>H', hybrid, 1024+34)
    unit = block_size // 512
    bitmap_sectors = (sectors // unit + 4095)//4096
    first = max(old_first, bitmap+bitmap_sectors)
    first += (sectors-2-first) % unit
    blocks = (sectors-2-first) // unit
    if old_blocks > blocks or not (3 <= bitmap < old_first):
        raise ValueError("HFS template exceeds planned partition")
    volume = bytearray(sectors*512)
    volume[:old_first*512] = hybrid[:old_first*512]
    volume[first*512:(first+old_blocks*unit)*512] = hybrid[old_first*512:]
    for bit in range(old_blocks, blocks):
        volume[bitmap*512+bit//8] &= ~(0x80 >> (bit%8))
    struct.pack_into('>H', volume, 1024+18, blocks)
    struct.pack_into('>H', volume, 1024+28, first)
    struct.pack_into('>H', volume, 1024+34, old_free+blocks-old_blocks)
    volume[-1024:-512] = volume[1024:1536]
    return bytes(volume)


def build_volume(output, loader, product):
    with tempfile.TemporaryDirectory(dir=output.parent) as tmp:
        cd = Path(tmp) / 'cd'
        cd.mkdir()
        for source in (loader, product):
            shutil.copyfile(source, cd / source.name)
        disk = Path(tmp) / 'disk'
        subprocess.run(['genisoimage', '-quiet', '-hfs', '-part', '-no-desktop',
                        '-V', 'OS32BOOT', '-o', disk, cd], check=True)
        volume = Path(tmp) / 'volume'
        volume.write_bytes(hdd_volume(partition(disk.read_bytes())))
        volume.replace(output)


if __name__ == '__main__':
    if sys.argv[1] == 'pair':
        from root_pair import wire
        data = Path(sys.argv[2]).read_bytes()
        _, offset, _, _ = wire(data)
        print(hex(struct.unpack_from('<I', data, offset)[0]))
    else:
        output, loader, product = map(Path, sys.argv[2:])
        product_load(product.read_bytes())
        build_volume(output, loader, product)
