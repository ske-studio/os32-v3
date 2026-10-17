#!/usr/bin/env python3
"""T4a-2: real VA0 SQLite/S4 controls, write-free rejection and mutants.

Run make sqlite-module-proto first. Mutants are provided for PM's post-review
MUTATE=1 run; implementers run only normal controls.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import mkmod
import gen_sqlite_imports
from test_module_image import build_validator, validate, fixture, crc, word, POLICY
from test_mkmod import rejected


def check(run, rc, name):
    expected = b'GREEN\n' if rc == 0 else f'RED {rc:02}\n'.encode()
    assert run.returncode == bool(rc) and run.stdout == expected, (
        name, run.returncode, run.stdout, run.stderr)


def relocation_suite(exe, runner):
    # First relocation is valid. The second is invalid so an eager writer is
    # caught by the full-buffer comparison in the C harness (including BSS).
    for base in (0, 0x1200000, 0xffffefff):
        check(validate(exe, fixture(), relocate_base=base, runner=runner, **POLICY), 0, 'unaligned')
    check(validate(exe, fixture(), relocate_base=0xfffff000, runner=runner, **POLICY),
          11, 'base overflow')
    for name, offset in (('tail-minus-two', 62), ('BSS', 64), ('rounding', 1000),
                         ('duplicate', 1), ('partial overlap', 3), ('import overlap', 32),
                         ('prefix-as-offset', 0xfffffffc)):
        bad = fixture()
        word(bad, 37, offset)
        check(validate(exe, crc(bad), relocate_base=0x1200000, runner=runner, **POLICY), 11, name)
    # Exact last four payload bytes are valid.
    tail = fixture()
    word(tail, 37, 60)
    struct.pack_into('<I', tail, 4096 + 60, 64)
    check(validate(exe, crc(tail), relocate_base=0x1200000, runner=runner, **POLICY), 0, 'last word')


def real_suite(exe, directory, runner, packer=mkmod):
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    metadata = json.loads((directory / 'sqlite_imports.json').read_text())
    elf = (directory / 'sqlite.elf').read_bytes()
    image, stats = packer.build(elf, metadata, b'', spec['version'],
                               spec['generations']['memory_layout'], module_id=2)
    assert image == (directory / 'sqlite.bin').read_bytes(), 'stale prototype image'
    parsed = mkmod.Elf(elf)
    assert parsed.symbol('sqlite_module_pool')['size'] == 4
    assert 'sqlite_mem_pool' not in parsed.symbols and 'tick_count' not in parsed.symbols
    assert len(metadata['imports']) == 23 and len(metadata['imports']) <= 32
    assert all(e['context'] == 'normal' and not e['irq'] for e in metadata['imports'])
    assert not {'memcpy', 'memset', 'tick_count'} & {e['name'] for e in metadata['imports']}
    assert stats['mem_size'] < 384 * 1024, 'static pool was retained'
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=metadata['module_abi_gen'], module_id=2, ops=1, img_valid=0,
                    imports=[(mkmod.fnv1a(e['name'].encode()), e['type_ver'], e['kind'])
                             for e in metadata['imports']],
                    exports=[(1, mkmod.fnv1a(b'initialize'), 1)], runner=runner)
    check(validate(exe, image, **contract), 0, 'real SQLite validate')
    check(validate(exe, image, relocate_base=0x1200000, **contract), 0, 'real SQLite relocate')
    check(validate(exe, image, relocate_base=0xffffffff - stats['mem_size'] + 1, **contract),
          11, 'real SQLite base overflow')
    h = struct.unpack_from('<36I', image)
    for field, value in ((22, 4097), (24, 33), (26, 2), (10, 528384)):
        bad = bytearray(image)
        word(bad, field, value)
        check(validate(exe, crc(bad), relocate_base=0x1200000, **contract), 3, 'SQLite policy cap')
    # Separate image/load-span caps with all other ranges still legal.
    for prefix, memory in ((147456, h[10]), (143360, 524288)):
        bad = bytearray(image[:h[11]])
        bad.extend(bytes(prefix - len(bad)))
        bad.extend(image[h[11]:])
        for field, value in ((3, len(bad)), (11, prefix), (10, memory)):
            word(bad, field, value)
        check(validate(exe, crc(bad), relocate_base=0x1200000, **contract), 3, 'image/load span cap')
    # Producer refusal based on actual linked relocations/symbols, not synthetic metadata.
    rel = next(s for s in parsed.sections if s['type'] == 9 and s['size'] and
               parsed.sections[s['info']]['flags'] & 2)
    info = struct.unpack_from('<I', elf, rel['offset'] + 4)[0]
    symtab = parsed.sections[rel['link']]
    symoff = symtab['offset'] + (info >> 8) * 16
    def build(data):
        return packer.build(data, metadata, b'', spec['version'],
                            spec['generations']['memory_layout'], module_id=2)
    bad = bytearray(elf)
    struct.pack_into('<H', bad, symoff + 14, 0)
    rejected(lambda: build(bad), 'unresolved')
    bad = bytearray(elf)
    struct.pack_into('<H', bad, symoff + 14, 0xfff1)
    rejected(lambda: build(bad), 'allocated body symbol')
    bad = bytearray(elf)
    struct.pack_into('<I', bad, rel['offset'] + 4, (info & ~255) | 3)
    rejected(lambda: build(bad), 'relocation type')
    # R_386_32 addends cannot hide MMIO or one-past without the named end contract.
    for section in parsed.sections:
        if section['type'] != 9 or not section['size']:
            continue
        target = parsed.sections[section['info']]
        if not target['flags'] & 2:
            continue
        records = parsed.section_data(section)
        for at in range(0, len(records), 8):
            where, rinfo = struct.unpack_from('<II', records, at)
            if rinfo & 255 != 1:
                continue
            bad = bytearray(elf)
            struct.pack_into('<I', bad, target['offset'] + where - target['addr'], stats['mem_size'])
            rejected(lambda: build(bad), 'value outside body')
            # Only the explicitly named end symbol may carry body one-past.
            strings = parsed.section_data(parsed.sections[parsed.sections[section['link']]['link']])
            end_name = strings.index(b'__module_end\0')
            symbol_at = parsed.sections[section['link']]['offset'] + (rinfo >> 8) * 16
            struct.pack_into('<II', bad, symbol_at, end_name, stats['mem_size'])
            build(bad)
            return stats
    raise AssertionError('no absolute relocation in real SQLite')


MUTATIONS = (
    ('payload-boundary', '!within(off, 4, h[MI_PAYLOAD_SIZE])', '!within(off, 2, h[MI_PAYLOAD_SIZE])'),
    ('base-overflow', 'if (base > 0xffffffffUL - h[MI_MEM_SIZE])', 'if (0)'),
    ('import-crossing', 'if (overlap(off, 4, slot, 4))', 'if (0)'),
    ('duplicate-write', 'if (overlap(off, 4, prev, 4))', 'if (0)'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=ROOT / 'build/out/sqlite-proto')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='sqlite-module-') as td:
        folder = Path(td)
        # Regeneration must be deterministic and separate from KDEBUG's 235 slots.
        generated = gen_sqlite_imports.generate(folder)
        for name in ('sqlite_imports.json', 'sqlite_imports.h', 'sqlite_import_stubs.S'):
            assert (folder / name).read_bytes() == (args.directory / name).read_bytes()
        assert generated['module_abi_gen'] != json.loads(
            (ROOT / 'kdebug/generated/kdebug_imports.json').read_text())['module_abi_gen']
        exe = build_validator(folder)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            relocation_suite(exe, runner)
            stats = real_suite(exe, args.directory, runner)
            print(f'PASS {runner}: unaligned/last-word reloc; tail-2/BSS/duplicate/import/overflow write=0')
        print(json.dumps(stats, sort_keys=True))
        if args.mutate:
            source = (ROOT / 'kernel/module_image.c').read_text()
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, name
                path = folder / 'module_image.c'
                path.write_text(source.replace(old, new).replace('"../lib/crc32_core.inc"',
                                '"' + str(ROOT / 'lib/crc32_core.inc') + '"'))
                mutant = build_validator(folder, path)
                try:
                    relocation_suite(mutant, 'qemu')
                except AssertionError as exc:
                    # Only a normal return/output mismatch counts; never a crash.
                    assert exc.args and exc.args[0][1] in (0, 1, 2), exc
                    print('MUTATE', name, 'RED (runtime observation)')
                else:
                    raise AssertionError('surviving mutant: ' + name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
