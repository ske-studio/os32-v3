"""Finalize the PowerMac ELF from an ISO and the selected kernel link command."""
import ctypes
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

import lz4.block
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mkmod import Elf, require, span
from module_schema import CONSTANTS, HEADER_FIELDS, MODULE_FORMATS
from root_lz4_layout import PAGE, STAGING_BASE, STAGING_SIZE, round_page, segments

MAX_FRAME = STAGING_SIZE


def wire(data):
    elf = Elf(data, 'ppc-powermac')
    sym = lambda name: elf.symbol(name)['value']
    loads = sorted((p for p in segments(data) if p[0] == 1), key=lambda p: p[2])
    require(len(loads) == 2 and elf.entry == sym('ppc_kernel_entry'), 'pair ELF entry/LOADs')
    low, high = loads
    require(high[2] == STAGING_BASE, 'root staging address')
    fields = [elf.entry]
    for p, names in zip(loads, [('__kernel_image_start', '__kernel_file_end', '__kernel_load_mem_end'),
                                ('__root_image_start', '__root_file_end', '__root_load_end')]):
        _, off, va, pa, size, mem, flags, align = p
        require((va, va + size, va + mem) == tuple(map(sym, names)) and va == pa and
                size <= mem and align > 0 and not (align & (align-1)) and (va-off) % align == 0, 'LOAD symbols')
        span(data, off, size)
        fields.extend((va, size, mem))
    require(low[2] <= elf.entry < low[2]+low[4] and low[2]+low[5] <= high[2]
            and low[1]+low[4] <= high[1] and high[4] == high[5] and high[6] == 4,
            'overlapping or invalid LOADs')
    rec = sym('__root_record_start')
    require(low[2] <= rec and rec + 16 <= low[2]+low[4] and
            rec+16 == sym('__root_record_end'), 'pair record')
    fields.extend((high[2], high[4], rec, 16))
    offset = rec - low[2]
    body = bytearray(span(data, low[1], low[4]))
    body[offset:offset+16] = bytes(16)
    return struct.pack('<11I', *fields) + body + bytes(high[4]), low[1]+offset, high[1], high[4]


def bind_module(module, pair):
    fields = struct.unpack_from('<36I', module)
    hdr = dict(zip(HEADER_FIELDS, fields))
    fmt = MODULE_FORMATS['ppc-powermac']
    require((hdr['FORMAT'], hdr['RESERVED0'], hdr['HEADER_SIZE'], hdr['IMAGE_SIZE']) ==
            (fmt.version, fmt.tag, CONSTANTS['HEADER_SIZE'], len(module)), 'invalid PPC KMOD')
    image = bytearray(module)
    crc_off = 4 * HEADER_FIELDS.index('IMAGE_CRC')
    struct.pack_into('<I', image, crc_off, 0)
    require(zlib.crc32(image) == hdr['IMAGE_CRC'], 'input KMOD CRC')
    if hdr['FLAGS'] & CONSTANTS['FLAG_PAIR']:
        struct.pack_into('<I', image, 4 * HEADER_FIELDS.index('PAIR_CRC'), pair)
    struct.pack_into('<I', image, crc_off, zlib.crc32(image))
    return image


def bind_iso(raw, pair):
    """Visit plain ISO9660 file extents, preserving directory records and dates."""
    image = bytearray(raw)
    pvd = span(raw, 16*2048, 2048)
    require(pvd[:7] == b'\x01CD001\x01', 'invalid primary volume descriptor')
    def extent(record):
        return (2048 * struct.unpack_from('<I', record, 2)[0],
                struct.unpack_from('<I', record, 10)[0])
    pending = [extent(pvd[156:190])]
    seen = set()
    while pending:
        start, size = pending.pop()
        require(start not in seen, 'duplicate directory')
        seen.add(start)
        directory = span(raw, start, size)
        pos = 0
        while pos < size:
            length = directory[pos]
            if not length:
                pos = (pos//2048+1)*2048
                continue
            record = span(directory, pos, length)
            pos += length
            name = span(record, 33, record[32])
            if name in (b'\x00', b'\x01'):
                continue
            off, count = extent(record)
            if record[25] & 2:
                pending.append((off, count))
            else:
                file = span(raw, off, count)
                if file.startswith(b'KMOD'):
                    image[off:off+count] = bind_module(file, pair)
    return bytes(image)


def compress(raw):
    # CI's Python wheel can bundle a different liblz4 than the distro one.
    # Select the system HC encoder explicitly so the same input yields the
    # same raw block in both environments.
    lib = ctypes.CDLL('liblz4.so.1')
    require(lib.LZ4_versionNumber() == 10904, 'root requires liblz4 1.9.4')
    capacity = lib.LZ4_compressBound(len(raw))
    require(capacity > 0, 'ISO exceeds LZ4 input limit')
    buf = ctypes.create_string_buffer(capacity)
    count = lib.LZ4_compress_HC(raw, buf, len(raw), capacity, 12)
    require(count > 0, 'LZ4 HC12 compression failed')
    return buf.raw[:count]


def build(iso, output, link):
    original = iso.read_bytes()
    provisional = bind_iso(original, 0)
    initial_block = compress(provisional)
    size = round_page(len(initial_block)) + PAGE
    require(link[-2:] == ['-o', str(output)], 'missing link output')
    while size <= MAX_FRAME:
        with tempfile.TemporaryDirectory(dir=output.parent) as work:
            path = Path(work)/'kernel.elf'
            subprocess.run([*link[:-2], f'-Wl,--defsym,root_frame_size={size}', '-o', path], check=True)
            data = bytearray(path.read_bytes())
            payload, rec, off, frame = wire(data)
            require(frame == size, 'linked frame size')
            data[off:off+len(initial_block)] = initial_block
            struct.pack_into('<4I', data, rec, 0, zlib.crc32(provisional),
                             len(initial_block), len(provisional))
            payload, *_ = wire(data)
            pair = zlib.crc32(payload)
            raw = bind_iso(original, pair)
            block = compress(raw)
            if len(block) > size:
                size = round_page(len(block)) + PAGE
                continue
            data[off:off+frame] = block.ljust(frame, b'\0')
            struct.pack_into('<4I', data, rec, pair, zlib.crc32(raw), len(block), len(raw))
            # Only the two fixed-size payloads are replaced; all section headers
            # and symbols stay in the original linked file.
            final_wire, *layout = wire(data)
            require(payload == final_wire and layout == [rec, off, frame], 'final ELF layout/pair')
            expected, crc, packed, length = struct.unpack_from('<4I', data, rec)
            restored = lz4.block.decompress(data[off:off+packed], uncompressed_size=length)
            require(zlib.crc32(final_wire) == expected and len(restored) == length and
                    restored == raw and zlib.crc32(restored) == crc, 'final root verification')
            path.write_bytes(data)
            final_iso = Path(work)/'root.iso'
            final_iso.write_bytes(raw)
            # Withdraw the old ELF before either verified output changes.
            output.unlink(missing_ok=True)
            final_iso.replace(output.parent/'root.iso')
            path.replace(output)
            return
    raise ValueError(f'root frame {size:#x} exceeds staging capacity {MAX_FRAME:#x}')


if __name__ == '__main__':
    iso, output, separator, *link = sys.argv[1:]
    require(separator == '--', 'expected link command')
    build(Path(iso), Path(output), link)
