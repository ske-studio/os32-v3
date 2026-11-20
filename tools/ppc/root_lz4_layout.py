"""ELF32 BE program headers and page rounding for the PowerMac root LOAD."""
import struct

PAGE = 4096


def segments(data):
    if data[:6] != b'\x7fELF\x01\x02':
        raise ValueError('expected a big-endian ELF32')
    start, = struct.unpack_from('>I', data, 28)
    stride, count = struct.unpack_from('>HH', data, 42)
    return [struct.unpack_from('>8I', data, start + i * stride) for i in range(count)]


def round_page(size):
    return (size + PAGE - 1) // PAGE * PAGE
