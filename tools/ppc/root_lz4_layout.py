"""ELF32 BE program headers and page rounding for the PowerMac root LOAD."""
import struct
import re
from pathlib import Path

MEMMAP = (Path(__file__).resolve().parents[2] / 'include/memmap.h').read_text()


def layout_constant(name):
    match = re.search(r'^#define\s+' + name + r'\s+(0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\s*(?:/\*.*?\*/)?$', MEMMAP, re.M)
    if not match:
        raise ValueError(f'expected integer constant {name} in include/memmap.h')
    return int(match[1], 0)


STAGING_BASE = layout_constant('MEM_BOOT_STAGING_BASE')
STAGING_SIZE = layout_constant('MEM_BOOT_STAGING_SIZE')
PAGE = layout_constant('MEM_PAGE_SIZE')


def segments(data):
    if data[:6] != b'\x7fELF\x01\x02':
        raise ValueError('expected a big-endian ELF32')
    start, = struct.unpack_from('>I', data, 28)
    stride, count = struct.unpack_from('>HH', data, 42)
    return [struct.unpack_from('>8I', data, start + i * stride) for i in range(count)]


def round_page(size):
    return (size + PAGE - 1) // PAGE * PAGE
