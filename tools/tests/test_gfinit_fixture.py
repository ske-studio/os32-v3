"""S1: compare compiled fixture with external init_scripts.txt; no ROM needed.

Missing txt is an error, never a silent skip. CI/PM may set GFINIT_SOURCE or
--source. --mutate always runs the positive control before two broken headers.
No decoder, bus, permission mechanism or S2 runtime is implemented here.
"""
import argparse
import collections
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from gfinit_source import read_source, effects
from gfinit_rom_check import fixture_tables, check_rom

HEADER = ROOT / 'tools/tests/gfinit_fixture.h'
DEFAULT_SOURCE = pathlib.Path.home() / 'os32-tmp/run/gf6200_4a/nvbios/init_scripts.txt'
COUNTS = dict(zip(
    [0x32,0x33,0x36,0x37,0x38,0x39,0x4a,0x4b,0x51,0x52,0x53,0x54,0x58,
     0x5b,0x5f,0x62,0x63,0x65,0x6b,0x6e,0x6f,0x71,0x72,0x74,0x75,0x78,0x7a],
    [32,1,1,1,2,4,2,1,2,2,9,3,3,1,1,2,1,1,3,36,5,9,6,13,2,8,42]))


def compiled(header):
    with tempfile.TemporaryDirectory(prefix='gfinit-') as name:
        folder = pathlib.Path(name)
        (folder / 'gfinit_fixture.h').write_text(header)
        # Quoted include resolves beside the copied harness, not beside ROOT.
        source = folder / 'fixture.c'
        source.write_text((ROOT / 'tools/tests/gfinit_fixture_host.c').read_text())
        exe = folder / 'fixture'
        subprocess.run(['gcc', '-std=gnu11', '-m32', '-march=i386', '-Wall',
                        '-Wextra', '-Werror', '-ffreestanding', '-nostdlib',
                        '-static', '-fno-pie', '-no-pie', '-fno-stack-protector',
                        '-Wno-unused-const-variable', str(source), '-o', str(exe)],
                       check=True, capture_output=True)
        return host32.run([str(exe)], check=True, capture_output=True, timeout=30).stdout


def verify(header, source):
    scripts, rows = read_source(source)
    expected_effects = effects(rows)
    assert collections.Counter(r[2][0] for r in rows) == COUNTS
    wire = compiled(header)
    cursor = 0

    def take(fmt):
        nonlocal cursor
        result = struct.unpack_from(fmt, wire, cursor)
        cursor += struct.calcsize(fmt)
        return result

    assert take('<5I') == (0, 1, 2, 3, 4), 'main order'
    for span in [(0x5d4c, 5, 2, 0), (0x5d66, 4, 8, 0),
                 (0x5d86, 3, 12, 0), (0x5daa, 8, 9, 0),
                 (0x5c59, 8, 1, 0), (0x5bc8, 4, 0, 1)]:
        assert take('<4I') == span, 'support table length'
    assert 0x5daa + 8 * 9 == 0x5df2  # half-open end is SUB8, not table data
    expected_scripts = []
    for sid, start in scripts:
        rs = [r for r in rows if r[0] == sid]
        block = b''.join(r[2] for r in rs)
        main_mask = 1 << sid if sid <= 4 else 8 if sid in (8, 255) else 1
        flags = int(any(e[0] in {r[1] for r in rs} for e in expected_effects))
        expected_scripts.append((sid, start, len(block), zlib.crc32(block),
                                 rows.index(rs[0]), len(rs), main_mask, flags))
    for expected in expected_scripts:
        assert take('<8I') == expected, ('script', expected)
    for sid, pc, raw in rows:
        flags = 0
        for e in expected_effects:
            if e[0] == pc:
                flags |= e[-1]
        main_mask = 1 << sid if sid <= 4 else 8 if sid in (8, 255) else 1
        expected = (sid, pc, len(raw), raw[0], main_mask, flags, raw.ljust(44, b'\0'))
        assert take('<6I44s') == expected, ('instruction', hex(pc))
    for e in expected_effects:
        assert take('<11I') == e, ('effect', hex(e[0]), hex(e[5]))
    assert cursor == len(wire), 'extra table entries'
    # Independent design anchors: N1 remains denied without shared W policy;
    # RESET and strap/PRAMIN writes; all twelve compressed NV_REG positions.
    assert [e[0] for e in expected_effects if e[-1] & 2] == [0xe81e]
    assert all(e[-1] & 1 for e in expected_effects)
    for pc, reg in [(0xdf10, 0x184c), (0xdf10, 0x1850), (0xdf10, 0x200),
                    (0xdf1d, 0x101000), (0xe7f1, 0x70fff0), (0xe7fb, 0x1700)]:
        assert any(e[0] == pc and e[5] == reg for e in expected_effects)
    for start in (0x5dfa, 0x5e34, 0x5e6e):
        assert all(any(pc == start+13*i and raw[0] == 0x6e for _, pc, raw in rows)
                   for i in range(4))
    assert not any(e[1] == 0 and e[5] == 0x10020c for e in expected_effects)
    # Exercise notebook verifier on synthetic data, including negative paths.
    manifest, insns, _ = fixture_tables(header)
    assert manifest == expected_scripts
    synthetic = bytearray(0x10000)
    for _, pc, raw in rows:
        synthetic[pc:pc+len(raw)] = raw
    check_rom(synthetic, manifest, insns)
    for bad in (synthetic[:0xe832], synthetic[:0x5df2]):
        try:
            check_rom(bad, manifest, insns)
        except ValueError:
            pass
        else:
            raise AssertionError('truncated ROM accepted')
    synthetic[0xe81e] ^= 1
    try:
        check_rom(synthetic, manifest, insns)
    except ValueError:
        pass
    else:
        raise AssertionError('changed ROM accepted')
    return len(expected_effects)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=pathlib.Path,
                        default=pathlib.Path(os.environ.get('GFINIT_SOURCE', DEFAULT_SOURCE)))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    header = HEADER.read_text()
    count = verify(header, args.source)
    print(f'PASS: external txt = compiled C: 9 scripts, 193 instructions, 27 opcodes, {count} effects', flush=True)
    print('PASS: synthetic ROM verifier control/changed byte/truncation (actual ROM NOT tested)', flush=True)
    if args.mutate:
        mutations = [
            ('instruction-offset', 'GF_INSN(0, 0xdeb0,', 'GF_INSN(0, 0xdeb1,'),
            ('effect-value', '0xffffffff, 0x1fff, 0x0, 0x1)',
             '0xffffffff, 0x1ffe, 0x0, 0x1)'),
            ('N1-condition-flag', 'GF_INSN(3, 0xe81e, 6, 0x78, 8, 3,',
             'GF_INSN(3, 0xe81e, 6, 0x78, 8, 1,'),
        ]
        for name, old, new in mutations:
            assert old in header, ('mutation anchor', name)
            try:
                verify(header.replace(old, new, 1), args.source)
            except AssertionError as error:
                print(f'MUTATE {name}: detected {error}', flush=True)
            else:
                raise AssertionError(f'mutant survived: {name}')


if __name__ == '__main__':
    main()
