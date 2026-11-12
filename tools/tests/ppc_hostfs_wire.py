#!/usr/bin/env python3
"""Exercise the PLN1 external-payload boundary against Python wire vectors."""
import ctypes
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import ppc_lan_host as host

CHECK = {'id': 'ppc-hostfs-wire', 'covers': ['tools/tests/ppc_hostfs_wire.py'],
         'inputs': ['platform/powermac/lan_wire.c', 'platform/powermac/lan_boot.h',
                    'include/types.h', 'lib/kstring.h', 'lib/kstring_c.c',
                    'tools/ppc_lan_host.py']}
SOURCE = ROOT / 'platform/powermac/lan_wire.c'

class Header(ctypes.Structure):
    _fields_ = [('boot', ctypes.c_ubyte * 16), ('nonce', ctypes.c_ubyte * 16),
                ('seq', ctypes.c_ulonglong), ('records', ctypes.c_ulonglong),
                ('dropped', ctypes.c_ulonglong), ('request', ctypes.c_ulong),
                ('opcode', ctypes.c_ushort), ('status', ctypes.c_ushort),
                ('size', ctypes.c_ushort), ('flags', ctypes.c_ushort), ('kind', ctypes.c_ubyte)]


def exercise(source, directory):
    path = pathlib.Path(directory)
    (path / 'wire.c').write_text(source)
    lib = path / 'wire.so'
    subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-std=gnu11',
                    '-I', str(ROOT), '-I', str(ROOT / 'include'),
                    '-I', str(ROOT / 'platform/powermac'), str(path / 'wire.c'),
                    str(ROOT / 'lib/kstring_c.c'), '-o', str(lib)], check=True)
    c = ctypes.CDLL(str(lib))
    h = Header()
    h.kind, h.size = 10, 1044
    h.boot[:] = bytes(range(16))
    payload = bytes((i % 251 for i in range(1044)))
    expected = host.HEADER.pack(host.MAGIC, 1, 10, 80, bytes(range(16)),
                                0, 0, 0, 0, 0, 0, 1044, 0, 0, bytes(16)) + payload
    out = ctypes.create_string_buffer(1200)
    used = ctypes.c_ulong()
    assert c.pm_lan_encode_payload(out, 1200, ctypes.byref(h), payload, 1044, ctypes.byref(used)) == 0
    assert out.raw[:used.value] == expected
    decoded = Header()
    assert c.pm_lan_decode_header(expected, len(expected), ctypes.byref(decoded)) == 0
    assert decoded.kind == 10 and decoded.size == 1044
    assert c.pm_lan_decode_header(expected, len(expected) - 1, ctypes.byref(decoded)) != 0, 'short decode'
    assert c.pm_lan_encode_payload(out, 1200, ctypes.byref(h), payload, 1043, ctypes.byref(used)) != 0, 'mismatched encode'
    assert c.pm_lan_encode_payload(out, 100, ctypes.byref(h), payload, 1044, ctypes.byref(used)) != 0
    guard = ctypes.create_string_buffer(512)
    ctypes.memset(guard, 0xa5, 512)
    assert c.pm_lan_decode(expected, len(expected), guard) != 0
    assert guard.raw[400:] == bytes([0xa5]) * 112
    legacy_long = bytearray(expected)
    legacy_long[5] = host.REQ
    assert c.pm_lan_decode(bytes(legacy_long), len(legacy_long), guard) != 0
    assert guard.raw[400:] == bytes([0xa5]) * 112
    malformed = bytearray(expected)
    malformed[31] = 1
    assert c.pm_lan_decode_header(bytes(malformed), len(malformed), ctypes.byref(decoded)) != 0
    malformed = bytearray(expected)
    malformed[64] = 1
    assert c.pm_lan_decode_header(bytes(malformed), len(malformed), ctypes.byref(decoded)) != 0
    h.kind, h.size = 9, 1
    assert c.pm_lan_encode_payload(out, 1200, ctypes.byref(h), b'x', 1, ctypes.byref(used)) != 0
    old = host.encode(host.REQ, bytes(range(16)), payload=b'abc')
    assert c.pm_lan_decode_header(old, len(old), ctypes.byref(decoded)) == 0
    assert decoded.kind == host.REQ and decoded.size == 3


def main():
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory() as temp:
        exercise(source, temp)
        print('baseline PASS')
        if '--mutate' in sys.argv:
            mutations = [('short decode', 'len > bytes - PLN1_HEADER_BYTES)', '0)'),
                         ('mismatched encode', 'len != h->payload_bytes || !valid_header(h)',
                          '!valid_header(h)')]
            for index, (name, old, new) in enumerate(mutations):
                assert source.count(old) == 1
                directory = pathlib.Path(temp) / str(index)
                directory.mkdir()
                try:
                    exercise(source.replace(old, new), directory)
                except AssertionError as error:
                    if str(error) != name:
                        raise
                    print(name + ' RED')
                else:
                    raise RuntimeError(name + ' survived')

if __name__ == '__main__':
    main()
