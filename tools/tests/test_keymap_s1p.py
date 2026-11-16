#!/usr/bin/env python3
"""S1p: compare the generated table against the independent BSD fixture."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='keymap-s1p', tier=1, budget=20,
    covers=['tools/kbd_import.py', 'tools/keymap/pc98_s1p.c',
            'tools/keymap/pc98_s1p_sources.json', 'tools/tests/keymap_s1p_host.c',
            'tools/tests/test_keymap_s1p.py', 'tools/tests/host32.py',
            'tools/tests/input_pc98_bsd_fixture.kbd',
            'input/hal/keymap.h', 'include/types.h', 'include/callconv.h'],
    basis=['INPUT_KEYMAP_DESIGN §3 §8 S1p', 'INPUT_BIND_DESIGN §2a',
           'FreeBSD jp.pc98.kbd d869f4ade283'])

# Independently transcribed kbio.h values for terms present in the fixture.
WORDS = 'nul soh stx etx eot enq ack bel bs ht nl vt ff cr so si dle dc1 dc2 dc3 dc4 nak syn etb can em sub esc fs gs rs us'.split()
SPECIAL = {'nop': (0xf500, 1, 0), 'lshift': (0xf101, 0, 0),
           'clock': (0xf105, 0, 0), 'lalt': (0xf107, 0, 0), 'lctrl': (0xf103, 0, 0),
           'btab': (0xf500, 1, 8), 'meta': (0xf500, 1, 0x84),
           'slock': (0xf10c, 0, 0), 'boot': (0xf500, 1, 0x85),
           'debug': (0xf500, 1, 0x86), 'susp': (0xf500, 1, 0x87),
           'saver': (0xf500, 1, 0x88), 'nscr': (0xf500, 1, 10)}
OVERRIDES = {0:27, 15:9, 28:13, 53:0xf115, 54:18, 55:3, 56:22, 57:127,
             58:30, 59:29, 60:28, 61:31, 62:1, 63:5, 81:0xf116,
             82:0, 83:0, 84:0, 97:0, 98:0, 99:0, 100:0, 101:0, 102:0,
             103:0, 104:0, 105:0, 106:0, 107:0}


def convert(t):
    if len(t) == 3 and t[0] == t[2] == "'": return (ord(t[1]), 0, 0)
    if t in WORDS: return (WORDS.index(t), 0, 0)
    if t == 'del': return (127, 0, 0)
    if t in SPECIAL: return SPECIAL[t]
    m = re.fullmatch(r'(fkey|scr)([0-9]+)', t)
    if not m: raise ValueError(t)
    return (0xf500, 1, int(m[2]) + (26 if m[1] == 'fkey' else 10))


def fixture():
    rows, flags = [], []
    for line in (ROOT/'tools/tests/input_pc98_bsd_fixture.kbd').read_text().splitlines():
        if not re.match(r'^\d{3} ', line): continue
        parts = re.findall(r"'(?:[^'])'|'''|\S+", line)
        key = int(parts[0]); assert key == len(rows)
        cells = [convert(t) for t in parts[1:9]]
        if key in OVERRIDES: cells = [(OVERRIDES[key], 0, 0)] * 8
        if key in (96, 114): cells = [SPECIAL['nop']] * 8
        rows.append(cells); flags.append('OCNB'.index(parts[10]))
    assert len(rows) == 128
    return rows, flags


def run(ctx):
    rows, flags = fixture()
    source = ROOT/'tools/keymap/pc98_s1p.c'
    text = source.read_text()
    ref = Path('/home/hight/os32-tmp/ref/freebsd-kbd/share/syscons/keymaps/jp.pc98.kbd')
    with tempfile.TemporaryDirectory(prefix='keymap-s1p-') as d:
        tmp = Path(d)
        if ref.exists():
            regenerated = tmp/'regenerated.c'
            subprocess.run([sys.executable, '-B', str(ROOT/'tools/kbd_import.py'), str(ref), str(regenerated)], check=True)
            assert regenerated.read_text() == text
        else:
            print('SKIP source regeneration: FreeBSD reference absent')
        expected = ['#include "input/hal/keymap.h"', 'const KmExactCell expected[128][8] = {']
        expected += ['{' + ','.join('{%d,%d,%d}' % cell for cell in row) + '},' for row in rows]
        expected += ['};', 'const u8 expected_flags[128] = {' + ','.join(map(str, flags)) + '};']
        (tmp/'expected.c').write_text('\n'.join(expected))
        host = (ROOT/'tools/tests/keymap_s1p_host.c').read_text()
        def build(product, harness=host):
            (tmp/'host.c').write_text(harness)
            (tmp/'product.c').write_text(product)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                            '-static', '-no-pie', '-I'+str(ROOT), '-I'+str(ROOT/'include'),
                            str(tmp/'product.c'), str(tmp/'expected.c'),
                            str(tmp/'host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build(text) == 0
        print('PASS all cells, Caps/Num/Kana, bank fallback and inheritance')
        if ctx.mutate:
            for key, old, new in [(2, '0x001a, 0, 0', '0x001b, 0, 0'),
                                  (53, '0xf115, 0, 0', '0xf116, 0, 0')]:
                lines = text.splitlines(keepends=True)
                matches = [i for i, line in enumerate(lines) if line.rstrip().endswith('/* %03d */' % key)]
                assert len(matches) == 1 and old in lines[matches[0]]
                lines[matches[0]] = lines[matches[0]].replace(old, new, 1)
                assert build(''.join(lines)) != 0
                print('RED runtime: key', key, old, '->', new)
            for old, new in [('!!mode ^ !!altlock', '!!mode | !!altlock'),
                             ('column ^= 1;', 'column ^= 0;')]:
                assert host.count(old) == 1
                assert build(text, host.replace(old, new)) != 0
                print('RED runtime: selection fixture', old, '->', new)
            assert build(text) == 0
    return 0

if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
