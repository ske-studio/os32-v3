"""Neutralize only the float calling convention in GCC's PPC32 GNU attributes."""
import struct
import sys
from pathlib import Path


def neutralize(data):
    # The expected format has one GNU vendor record and one file subsection.
    if data[:1] != b'A' or len(data) < 14:
        raise ValueError('invalid GNU attribute format')
    vendor_size, = struct.unpack_from('>I', data, 1)
    vendor_end = data.index(0, 5)
    section = vendor_end + 1
    if vendor_size != len(data) - 1 or data[5:vendor_end] != b'gnu':
        raise ValueError('unexpected GNU vendor subsection')
    size, = struct.unpack_from('>I', data, section + 1)
    if data[section] != 1 or size != len(data) - section:
        raise ValueError('expected one file attribute subsection')
    # These four small integers have canonical single-byte ULEB128 encodings.
    attributes = data[section + 5:]
    if attributes != bytes((4, 9, 12, 1)):
        raise ValueError(f'unexpected PPC32 ABI attributes {attributes.hex()}')
    data[section + 6] &= ~3
    return data


if __name__ == '__main__':
    Path(sys.argv[2]).write_bytes(neutralize(bytearray(Path(sys.argv[1]).read_bytes())))
