"""OSRI v2 serialization; only final real changes, in the planned order."""
import struct
import zlib
import numpy as np
from osri_enc2_spec import codes, allowed
from osri_enc2_timing import cuts


def pack(palettes, orders, indices, budget):
    if indices.shape != (400, 640) or palettes.shape != (400, 16, 3):
        raise ValueError('OSRI requires 640x400 and 16 RGB444 slots')
    if np.any(indices > 15) or np.any(palettes < 0) or np.any(palettes > 15):
        raise ValueError('index or RGB444 component out of range')
    events = []
    for y in range(1, 400):
        if not allowed(palettes[y - 1], palettes[y], budget):
            raise ValueError('OUT budget exceeded')
        cuts(palettes[y - 1], palettes[y], orders[y], 640)
        for slot in orders[y]:
            events.append(struct.pack('<HBBHH', y, slot, 0, int(codes(palettes[y, slot])), 0))
    planes = b''.join(np.packbits((indices >> p) & 1, axis=1, bitorder='big').tobytes()
                      for p in range(4))
    size = 128096 + 8 * len(events)
    header = struct.pack('<4sHHHHBBHIIIIIIHHHHIIII', b'OSRI', 2, 64, 640, 400, 4, 1, 0,
                         size, 64, 96, 128000, 128096, len(events), 8, 0, 1, 0, 0, 0, 0, 0)
    data = bytearray(header + struct.pack('<16H', *codes(palettes[0])) + planes + b''.join(events))
    struct.pack_into('<I', data, 48, zlib.crc32(data))
    return bytes(data)
