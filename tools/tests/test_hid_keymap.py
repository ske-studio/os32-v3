#!/usr/bin/env python3
"""INPUT_KEYMAP_DESIGN §3・§7 S1h/S1ht、NetBSD hidkbdmap 8a12c4d9.

Independent fixed expectations run without the reference tree; regeneration is
optional. C objects are freestanding; no host libc or emulator is involved.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='hid-keymap', tier=1, budget=20,
    covers=['tools/tests/test_hid_keymap.py', 'tools/hid_keymap_import.py',
            'tools/keymap/netbsd_wscons_sources.json',
            'tools/keymap/fetch_netbsd_wscons.sh', 'sys/input/keymaps/hid_jp.c',
            'sys/hal/input/keymap.h', 'sys/include/types.h', 'sys/include/callconv.h',
            'sys/dev/usb/hid_kbd_map.c'],
    basis=['INPUT_KEYMAP_DESIGN §3・§7 S1h/S1ht', 'NetBSD hidkbdmap 8a12c4d9'])
COMMIT = '8a12c4d95f032e12f64013d7269443256fd26341'
VERSION = 1
# Independent provenance pins, not read from the generator's metadata.
PINS = {
    'sys/dev/hid/hidkbdmap.c': '4384c3d6960ab74b461a19eecc6a1aad4dd97f468119a6499aff1e975f404190',
    'sys/dev/wscons/wskbd.c': '0960b012054ae9e1158f9005b1cebbd99071630a10fde8ea404c1d052e1a3cce',
    'sys/dev/wscons/wskbdutil.c': '25ce0b4116566a91909f84a59f26fc07583fecc1ce6265618a6d34cecac3bb5a',
    'sys/dev/wscons/wsksymdef.h': '02fa0df610a3db3209bcb790455d57d07d1f9f210f627f953c88270ee804f9bc',
    'sys/dev/wscons/wsksymvar.h': '6784532dba2c048602adc3e480d6a78c98229d4da3ec9659c8d0361eb3e641e2',
}
# Pinned digest of all 256 five-cell entries; independent numeric fixtures below
# check pinned US/JP rows and wskbdutil expansion. These fixtures
# distinguish inheritance, singleton upcase, JP replacement, and keypad groups.
ENTRIES_SHA256 = '6cee0078089816c0722d58f691b84279a758cf3ab06fdc7212d592154cd9f58d'


# Fixed expectations: 撤去の前の hid_kbd_map.c (9e78dc73).
LEGACY_SC = bytes.fromhex(
    "ffffffff1d2d2b1f1220212217232425"
    "2f2e181910131e14162c112a15290102"
    "030405060708090a1c000e0f340b0c1a"
    "1b28282627ff30313271626364656667"
    "68696a6b525361ff60383e36393f373c"
    "3b3d3aff414540491c4a4b4c46474842"
    "43444e50ffffff4dffffffffffffffff"
    "ffffffffffffffffffffffffffffffff"
    "ffffffffff4fff33720d3551ffffffff"
    "ffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffff"
    "747073ff747073ffffffffffffffffff"
    "ffffffffffffffffffffffffffffffff"
)


def array(text, name, count):
    match = re.search(r'const u8 '+name+r'\['+str(count)+r'\] = \{(.*?)\};', text, re.S)
    assert match, name
    result = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', match[1])]
    assert len(result) == count, name
    return result


def validate(text):
    assert f'version {VERSION}\n' in text
    assert 'NetBSD/src commit '+COMMIT in text
    for name, digest in PINS.items():
        assert ' * SHA256 '+digest+'  '+name+'\n' in text
    rows = re.findall(r'    \{(0x[0-9a-f]{4}), \{(0x[0-9a-f]{4}), (0x[0-9a-f]{4})\}, '
                      r'\{(0x[0-9a-f]{4}), (0x[0-9a-f]{4})\}\}, /\* 0x([0-9a-f]{2}) (.*?) \*/', text)
    assert len(rows) == 256
    entries = []
    for usage, row in enumerate(rows):
        assert int(row[5], 16) == usage
        values = [int(x, 16) for x in row[:5]]
        names = row[6].split()
        assert len(names) == 5
        for value, name in zip(values, names):
            # KS_KP navigation values (e.g. Home 0xf295) must never be byteified.
            if 0xf220 <= value <= 0xf27f:
                assert name.startswith('KS_KP_')
        entries.append(values)
    fixtures = {
        0x00: [0xf500]*5, 0x03: [0xf500]*5,
        0x04: [0xf500, 0x61, 0x41, 0x61, 0x41],
        0x1f: [0xf500, 0x32, 0x22, 0x32, 0x22],
        0x27: [0xf500, 0x30, 0x30, 0x30, 0x30],
        0x2f: [0xf500, 0x40, 0x60, 0x40, 0x60],
        0x30: [0xf500, 0x5b, 0x7b, 0x5b, 0x7b],
        0x31: [0xf500, 0x5d, 0x7d, 0x5d, 0x7d],
        0x28: [0xf500, 0x0d, 0x0d, 0x0d, 0x0d],
        0x58: [0xf500, 0xf20d, 0xf20d, 0xf20d, 0xf20d],
        0x5f: [0xf500, 0xf295, 0xf237, 0xf295, 0xf237],
        0x68: [0xf500, 0xf30c, 0xf34c, 0xf30c, 0xf34c],
        0x90: [0xf500, 0xf113, 0xf113, 0xf113, 0xf113],
        0x91: [0xf500, 0xf116, 0xf116, 0xf116, 0xf116],
        0xe0: [0xf10e, 0xf103, 0xf103, 0xf103, 0xf103],
        0xe4: [0xf500, 0xf104, 0xf104, 0xf104, 0xf104],
        0xe3: [0xf500, 0xf110, 0xf110, 0xf110, 0xf110],
    }
    for usage, expected in fixtures.items():
        assert entries[usage] == expected, hex(usage)
    data = b''.join(v.to_bytes(2, 'big') for row in entries for v in row)
    assert hashlib.sha256(data).hexdigest() == ENTRIES_SHA256, 'all-usage entries'
    compat = array(text, 'hid_jp_compat_sc', 256)
    for u in range(256):
        assert compat[u] == {0x90: 0x72, 0x91: 0x51}.get(u, LEGACY_SC[u]), hex(u)
    assert compat[0xe3] == compat[0xe7] == 0xff
    bits = array(text, 'hid_jp_norepeat', 32)
    order = array(text, 'hid_jp_order', 64)
    no_keydata = array(text, 'hid_jp_no_keydata', 32)
    for u in range(256):
        lock = u in (0x39, 0x47, 0x53, 0x88, 0x90, 0x91)
        modifier = 0xe0 <= u <= 0xe7
        assert (bits[u//8] >> (u%8)) & 1 == int(lock or modifier), ('norepeat', hex(u))
        assert (order[u//4] >> (u%4*2)) & 3 == (0 if modifier else 1 if lock else 2), ('order', hex(u))
        assert (no_keydata[u//8] >> (u%8)) & 1 == int(u == 0x53)
    assert 'hid_jp_actions[2][2] = {{0x90, 1}, {0x91, 2}};' in text
    assert '2, 256, hid_jp_entries, hid_jp_compat_sc, hid_jp_norepeat,' in text
    assert '&hid_jp_extensions' in text
    assert 'hid_jp_order, hid_jp_actions, 2, hid_jp_no_keydata' in text
    header = (ROOT/'sys/hal/input/keymap.h').read_text()
    assert 'return sym >= 0xf220 && sym <= 0xf27f;' in header


def mutate(text):
    # Each anchor is unique; execute the same independent validator on each mutant.
    changes = [
        ('LANG1/2', 'hid_jp_actions[2][2] = {{0x90, 1}, {0x91, 2}};',
         'hid_jp_actions[2][2] = {{0x90, 2}, {0x91, 1}};'),
        ('LANG1-keysym', '    {0xf500, {0xf113, 0xf113}, {0xf113, 0xf113}}, /* 0x90',
         '    {0xf500, {0xf116, 0xf116}, {0xf116, 0xf116}}, /* 0x90'),
    ]
    for name, count, index, value in [('norepeat', 32, 28, 0), ('order', 64, 56, 0xaa),
                                      ('compat_sc', 256, 4, 0x1e)]:
        before = re.search(r'const u8 hid_jp_'+name+r'\['+str(count)+r'\] = \{.*?\};', text, re.S)[0]
        data = array(text, 'hid_jp_'+name, count)
        data[index] = value
        after = 'const u8 hid_jp_'+name+'['+str(count)+'] = {'+','.join('0x%02x' % n for n in data)+'};'
        changes.append((name, before, after))
    # Explicit JP override omission produces the inherited US at/quotedbl mapping.
    before = '    {0xf500, {0x0032, 0x0022}, {0x0032, 0x0022}}, /* 0x1f'
    changes.append(('JP-override', before, before.replace('0x0022', '0x0040')))
    for name, before, after in changes:
        assert text.count(before) == 1, name+' anchor'
        try:
            validate(text.replace(before, after))
        except AssertionError:
            print('RED runtime '+name)
        else:
            raise AssertionError('SURVIVED '+name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--source-dir', type=Path, default=Path.home()/'os32-tmp/ref/netbsd-wscons')
    args = parser.parse_args()
    manifest = json.loads((ROOT/'tools/keymap/netbsd_wscons_sources.json').read_text())
    assert manifest == dict(commit=COMMIT, files=PINS)
    path = ROOT/'sys/input/keymaps/hid_jp.c'
    text = path.read_text()
    validate(text)
    if args.mutate:
        mutate(text)
    spec = importlib.util.spec_from_file_location('hid_import', ROOT/'tools/hid_keymap_import.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    assert module.VERSION == VERSION
    if args.source_dir.exists():
        assert module.generate(args.source_dir).encode() == path.read_bytes(), 'regeneration bytes'
        print('PASS pinned-source regeneration (byte identical)')
        # Refuse a tampered original and a forged SOURCES.md independently.
        with tempfile.TemporaryDirectory() as folder:
            copied = Path(folder)
            for name in ['SOURCES.md', *PINS]:
                dest = copied/name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes((args.source_dir/name).read_bytes())
            target = copied/'sys/dev/hid/hidkbdmap.c'
            original = target.read_bytes()
            target.write_bytes(original+b'\n')
            try:
                module.generate(copied)
            except ValueError as exc:
                assert str(exc).startswith('source SHA256 mismatch:')
            else:
                raise AssertionError('tampered source accepted')
            target.write_bytes(original)
            provenance = copied/'SOURCES.md'
            provenance.write_text(provenance.read_text().replace(PINS['sys/dev/hid/hidkbdmap.c'], '0'*64))
            try:
                module.generate(copied)
            except ValueError as exc:
                assert str(exc).startswith('SOURCES.md SHA256 mismatch:')
            else:
                raise AssertionError('forged provenance accepted')
        print('PASS source/provenance SHA256 mismatch refusal')
    else:
        print('SKIP regeneration: reference directory absent')
    # Compile only, freestanding, using the supported 32-bit toolchain.
    cc = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))/'bin/i386-elf-gcc'
    compiler = [str(cc)] if cc.exists() else [shutil.which('gcc') or 'gcc', '-m32']
    with tempfile.TemporaryDirectory() as folder:
        subprocess.run([*compiler, '-std=gnu11', '-ffreestanding', '-fno-builtin',
                        '-Wall', '-Wextra', '-Werror', '-I.', '-Iinclude',
                        '-c', str(path), '-o', str(Path(folder)/'hid_jp.o')], cwd=ROOT, check=True)
    print('PASS HID map2: 256 usages, provenance, compatibility and metadata')
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
