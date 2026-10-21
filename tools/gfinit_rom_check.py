#!/usr/bin/env python3
"""Notebook: python3 tools/gfinit_rom_check.py /path/to/private.rom

Prints expected start/length/IEEE CRC32 with --list (no ROM needed).
Reads the whole ROM into memory but compares ONLY the nine init-data ranges.
Does not disassemble, write, copy or store ROM bytes. All expectations come from the S1 transcript reconstruction,
not a prior ROM validation. Mismatch is failure: do not update expectations
from ROM automatically; compare notebook nvbios output and review the cause.
The script/fixture pair can be copied to a notebook without the source txt.
"""
import argparse
import pathlib
import re
import zlib

FIXTURE = pathlib.Path(__file__).parent / 'tests/gfinit_fixture.h'


def fixture_tables(text):
    def table(name):
        return [tuple(int(v.strip(), 0) for v in match.split(','))
                for match in re.findall(r'^\s+' + name + r'\(([^\n]+)\),$', text, re.M)]
    return table('GF_SCRIPT'), table('GF_INSN'), table('GF_EFFECT')


def check_rom(rom, scripts, instructions):
    for sid, start, length, crc, first, count, _, _ in scripts:
        block = rom[start:start+length]
        expected = b''.join(bytes(r[6:]) for r in instructions[first:first+count])
        actual_crc = zlib.crc32(block)
        if len(block) != length or actual_crc != crc or block != expected:
            raise ValueError(f'script {sid}: ROM mismatch at {start:04x}: '
                             f'length {len(block)}/{length} CRC {actual_crc:08x}/{crc:08x}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', nargs='?', type=pathlib.Path)
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()
    scripts, instructions, _ = fixture_tables(FIXTURE.read_text())
    if len(scripts) != 9 or len(instructions) != 193:
        parser.error('fixture table missing or malformed')
    for sid, start, length, crc, _, count, _, _ in scripts:
        print(f'script={sid:3d} start=0x{start:04x} length={length:4d} '
              f'crc32={crc:08x} instructions={count}')
    if args.rom is None:
        if not args.list:
            parser.error('provide a private ROM path, or --list')
        return
    check_rom(args.rom.read_bytes(), scripts, instructions)
    print('PASS: nine init DATA ranges, bytes/length/CRC match; no x86 code inspected')


if __name__ == '__main__':
    main()
