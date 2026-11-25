#!/usr/bin/env python3
"""Expand pinned NetBSD KB_US -> KB_JP (wskbdutil fillmapentry/get_mapentry).

OS32 additions: LANG1/2 actions, compatibility projection, repeat/order metadata.
No source files are downloaded implicitly. See tools/keymap/fetch_netbsd_wscons.sh.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT/'tools/keymap/netbsd_wscons_sources.json'
VERSION = 1
DEFAULT_SOURCE = Path.home()/'os32-tmp/ref/netbsd-wscons'
# Frozen pre-S2h projection, audited against hid_kbd_sc by the independent test.
COMPAT = '''
ff ff ff ff 1d 2d 2b 1f 12 20 21 22 17 23 24 25
2f 2e 18 19 10 13 1e 14 16 2c 11 2a 15 29 01 02
03 04 05 06 07 08 09 0a 1c 00 0e 0f 34 0b 0c 1a
1b 28 28 26 27 ff 30 31 32 71 62 63 64 65 66 67
68 69 6a 6b 52 53 61 ff 60 38 3e 36 39 3f 37 3c
3b 3d 3a ff 41 45 40 49 1c 4a 4b 4c 46 47 48 42
43 44 4e 50 ff ff ff 4d ff ff ff ff ff ff ff ff
ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
ff ff ff ff ff 4f ff 33 72 0d 35 51 ff ff ff ff
72 51 ff ff ff ff ff ff ff ff ff ff ff ff ff ff
ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
74 70 73 ff 74 70 73 ff ff ff ff ff ff ff ff ff
ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
'''


def verified_sources(directory):
    manifest = json.loads(MANIFEST.read_text())
    provenance = (directory/'SOURCES.md').read_text()
    if manifest['commit'] not in provenance:
        raise ValueError('SOURCES.md commit mismatch')
    result = {}
    for name, digest in manifest['files'].items():
        data = (directory/name).read_bytes()
        if not re.search(r'(?m)^'+digest+r'  '+re.escape(name)+r'$', provenance):
            raise ValueError('SOURCES.md SHA256 mismatch: '+name)
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('source SHA256 mismatch: '+name)
        result[name] = data.decode()
    return manifest, result


def symbols(header):
    # All symbols used by these two pinned layouts are integer definitions.
    return {name: int(value, 0) for name, value in re.findall(
        r'^#define\s+(KS_\w+)\s+(0x[0-9a-fA-F]+|[0-9]+)\b', header, re.M)}


def expand(source, defs):
    clean = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    parents = {name: (base, table) for name, base, table in re.findall(
        r'KBD_MAP\((KB_\w+),\s*(0|KB_\w+),\s*(\w+)\)', clean)}
    if parents['KB_JP'] != ('KB_US', 'hidkbd_keydesc_jp'):
        raise ValueError('unexpected KB_JP inheritance')
    if parents['KB_US'] != ('0', 'hidkbd_keydesc_us'):
        raise ValueError('unexpected KB_US inheritance')
    void = defs['KS_voidSymbol']
    entries = [[void]*5 for _ in range(256)]
    names = [['KS_voidSymbol']*5 for _ in range(256)]

    def upper(name):
        value = defs[name]
        if defs['KS_f1'] <= value <= defs['KS_f20']:
            return 'KS_F'+str(value-defs['KS_f1']+1)
        # Pinned US/JP singleton Plain symbols needing upcase are ASCII only.
        if 0x61 <= value <= 0x7a:
            return 'KS_'+chr(value).upper()
        if value <= 0xff and chr(value).upper() != chr(value):
            raise ValueError('unsupported non-ASCII singleton upcase: '+name)
        return name

    for layout in ('KB_US', 'KB_JP'):
        table = parents[layout][1]
        body = re.search(r'\b'+table+r'\[\]\s*=\s*\{(.*?)\};', clean, re.S)[1]
        for match in re.finditer(r'KC\((\d+)\),(.*?)(?=KC\(|\Z)', body, re.S):
            usage = int(match[1])  # NetBSD KC() uses decimal, never hex.
            cells = re.findall(r'KS_\w+', match[2])
            command = 'KS_voidSymbol'
            first = cells[0]
            if 0xf400 <= defs[first] < 0xf500 or first in ('KS_Cmd', 'KS_Cmd1', 'KS_Cmd2'):
                command = cells.pop(0)
            if not 0 <= usage < 256 or not 1 <= len(cells) <= 4:
                raise ValueError('invalid keymap row')
            if len(cells) == 1:
                cells.append(upper(cells[0]))
            if len(cells) == 2:
                cells += cells[:]
            elif len(cells) == 3:
                cells.append(upper(cells[2]))
            names[usage] = [command]+cells
            entries[usage] = [defs[name] for name in names[usage]]
    for usage, name in ((0x90, 'KS_Hiragana_Katakana'), (0x91, 'KS_Muhenkan')):
        names[usage] = ['KS_voidSymbol']+[name]*4
        entries[usage] = [void]+[defs[name]]*4
    return entries, names


def generate(directory):
    manifest, sources = verified_sources(directory)
    defs = symbols(sources['sys/dev/wscons/wsksymdef.h'])
    entries, names = expand(sources['sys/dev/hid/hidkbdmap.c'], defs)
    compat = bytes.fromhex(COMPAT)
    assert len(compat) == 256
    # OS32 lock ordering intentionally differs from FreeBSD (design K10).
    locks = {0x39, 0x47, 0x53, 0x88, 0x90, 0x91}
    no_repeat = locks | set(range(0xe0, 0xe8))
    bits = [sum((u in no_repeat) << (u % 8) for u in range(b, b+8))
            for b in range(0, 256, 8)]
    order = [0 if 0xe0 <= u <= 0xe7 else 1 if u in locks else 2 for u in range(256)]
    packed = [sum(order[b+i] << (2*i) for i in range(4)) for b in range(0, 256, 4)]
    # Retain the complete upstream copyright and redistribution terms.
    license_text = '\n'.join(re.findall(r'/\*.*?\*/', sources[name], re.S)[1]
        for name in ('sys/dev/hid/hidkbdmap.c', 'sys/dev/wscons/wsksymdef.h'))
    lines = ['/* Generated by tools/hid_keymap_import.py; version '+str(VERSION),
             ' * NetBSD/src commit '+manifest['commit']]
    lines += [' * SHA256 '+digest+'  '+name for name, digest in manifest['files'].items()]
    lines += [' * DO NOT EDIT. Regenerate with the pinned reference sources.',
              ' * Each usage is listed; KS_voidSymbol rows have no mapped meaning.',
              ' * Metadata accessors belong to S2h, not this generated table.', ' */',
              license_text, '#include "sys/hal/input/keymap.h"', '',
              'const KmEntry hid_jp_entries[256] = {']
    for usage, row in enumerate(entries):
        lines.append('    {0x%04x, {0x%04x, 0x%04x}, {0x%04x, 0x%04x}}, /* 0x%02x %s */'
                     % (*row, usage, ' '.join(names[usage])))
    lines += ['};', '']

    def array(name, data):
        lines.append('const u8 '+name+'['+str(len(data))+'] = {')
        for start in range(0, len(data), 16):
            lines.append('    '+', '.join('0x%02x' % n for n in data[start:start+16])+',')
        lines.extend(['};', ''])

    array('hid_jp_compat_sc', compat)
    array('hid_jp_norepeat', bits)
    array('hid_jp_order', packed)
    # KmHidExtensions is shared through input/hal/keymap.h.
    lines += ['const u8 hid_jp_actions[2][2] = {{0x90, 1}, {0x91, 2}};', '']
    array('hid_jp_no_keydata', [8 if b == 0x50 else 0 for b in range(0, 256, 8)])
    lines += ['const KmHidExtensions hid_jp_extensions = {',
              '    hid_jp_order, hid_jp_actions, 2, hid_jp_no_keydata', '};', '',
              'const KmBackend hid_jp_backend = {',
              '    2, 256, hid_jp_entries, hid_jp_compat_sc, hid_jp_norepeat,',
              '    &hid_jp_extensions', '};', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--output', type=Path, default=ROOT/'sys/input/keymaps/hid_jp.c')
    args = parser.parse_args()
    try:
        output = generate(args.source_dir)
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(1, str(exc)+'\n')
    args.output.write_text(output)


if __name__ == '__main__':
    main()
