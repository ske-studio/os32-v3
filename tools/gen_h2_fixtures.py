#!/usr/bin/env python3
"""T2h isolated, intentionally invalid images. Never a deployment input.
Only damages current build outputs; no old valid stamp or packaging bypass.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'sdk'))
import os32x_hdr as H

INPUTS = {
    'app': 'userland/tests/h2_stack.bin',
    'shell': 'userland/shell.bin',
    'shlib': 'userland/libos32gui.shlib',
    'dependent': 'userland/tests/gui_demo.bin',
}


def generate(out, root=ROOT):
    out.mkdir(parents=True, exist_ok=True)
    records = {}
    controls = {}
    def emit(name, data, source, reason):
        path = out / (name + '.fixture')
        path.write_bytes(data)
        records[path.name] = dict(source=source, sha256=hashlib.sha256(data).hexdigest(),
                                  bytes=len(data), expected=reason)
    for kind, source in INPUTS.items():
        data = (root / source).read_bytes()
        h = H.parse_header(data)
        if (h['version'] != H.OS32X_HDR_VERSION or h['header_size'] != H.OS32X_HDR_SIZE or
                h['kapi_abi_generation'] != H.OS32_KAPI_ABI_GENERATION or
                h['memory_layout_generation'] != H.OS32_MEMORY_LAYOUT_GENERATION):
            raise ValueError('rebuild current control: ' + source)
        controls[kind] = data
        emit('control-' + kind, data, source, 'valid control')
    for kind in ('app', 'shell', 'shlib'):
        data = controls[kind]
        # Synthetic old-format header from current fields, with the original body.
        old = bytearray(data[:H.OS32X_HDR_V3_SIZE])
        struct.pack_into('<I', old, 4, H.OS32X_HDR_V3_SIZE)
        struct.pack_into('<I', old, 8, 3)
        emit('old-' + kind, bytes(old) + data[H.OS32X_HDR_SIZE:], INPUTS[kind], 'unknown format or flags')
    for name, offset, value, reason in (
        ('unknown-format', 8, H.OS32X_HDR_VERSION + 1, 'unknown format or flags'),
        ('bad-abi-generation', 48, H.OS32_KAPI_ABI_GENERATION + 1, 'generation or KAPI layout mismatch'),
        ('bad-memory-generation', 52, H.OS32_MEMORY_LAYOUT_GENERATION + 1, 'generation or KAPI layout mismatch'),
        ('old-cpl0-flag', 12, H.parse_header(controls['app'])['flags'] | 4, 'unknown format or flags'),
    ):
        damaged = bytearray(controls['app'])
        struct.pack_into('<I', damaged, offset, value)
        emit(name, damaged, INPUTS['app'], reason)
    for kind in ('app', 'shlib'):
        damaged = bytearray(controls[kind])
        struct.pack_into('<I', damaged, 52, H.OS32_MEMORY_LAYOUT_GENERATION - 1)
        emit('prev-memory-generation-' + kind, damaged, INPUTS[kind],
             'generation or KAPI layout mismatch')
    damaged = bytearray(controls['shell'])
    struct.pack_into('<I', damaged, 48, H.OS32_KAPI_ABI_GENERATION + 1)
    emit('bad-shell-generation', damaged, INPUTS['shell'], 'generation or KAPI layout mismatch')
    damaged = bytearray(controls['shlib'])
    struct.pack_into('<I', damaged, 56, H.OS32_SHLIB_PROTOCOL + 1)
    emit('bad-shlib-generation', damaged, INPUTS['shlib'], 'generation or KAPI layout mismatch')
    (out / 'manifest.json').write_text(json.dumps(records, indent=2, sort_keys=True) + '\n')
    return records


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    args = parser.parse_args()
    generate(args.out)
