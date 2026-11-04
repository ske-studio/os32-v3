"""Independent host decoder/validator. Does not relax the guest loader contract."""
import struct
import zlib
import numpy as np


def decode(data, budget=47):
    if len(data) < 128096:
        raise ValueError('truncated OSRI')
    h = struct.unpack_from('<4sHHHHBBHIIIIIIHHHHIIII', data)
    fixed = (b'OSRI', 2, 64, 640, 400, 4, 1, 0, len(data), 64, 96, 128000, 128096)
    if h[:13] != fixed or h[14:18] != (8, 0, 1, 0) or any(h[19:]):
        raise ValueError('header or reserved field')
    count = h[13]
    if count > 6384 or len(data) != 128096 + count * 8:
        raise ValueError('event capacity or length')
    copy = bytearray(data)
    copy[48:52] = bytes(4)
    if zlib.crc32(copy) != h[18]:
        raise ValueError('CRC32')
    q = list(struct.unpack_from('<16H', data, 64))
    if any(c > 4095 for c in q):
        raise ValueError('initial RGB444')
    palettes = np.empty((400, 16, 3), dtype=np.int32)
    orders = [[] for _ in range(400)]
    rows = [[] for _ in range(400)]
    previous_line = 0
    for off in range(128096, len(data), 8):
        y, slot, r1, color, r2 = struct.unpack_from('<HBBHH', data, off)
        if not 1 <= y < 400 or y < previous_line or slot >= 16 or r1 or r2 or color > 4095:
            raise ValueError('invalid event')
        if slot in orders[y]:
            raise ValueError('duplicate slot')
        previous_line = y
        orders[y].append(slot)
        rows[y].append((slot, color))
    outs = []
    for y in range(400):
        changed_components = 0
        for slot, color in rows[y]:
            if q[slot] == color:
                raise ValueError('redundant event')
            changed_components += sum((q[slot] >> s & 15) != (color >> s & 15) for s in (8, 4, 0))
            q[slot] = color
        cost = changed_components + max(0, len(rows[y]) - 1)
        if cost > min(budget, 47):
            raise ValueError('OUT budget')
        if y:
            outs.append(cost)
        palettes[y] = [[c >> 8, c >> 4 & 15, c & 15] for c in q]
    indices = np.zeros((400, 640), dtype=np.uint8)
    for p in range(4):
        plane = np.frombuffer(data, dtype=np.uint8, count=32000, offset=96 + p * 32000)
        indices |= np.unpackbits(plane, bitorder='big').reshape(400, 640) << p
    return palettes, [tuple(o) for o in orders], indices, outs
