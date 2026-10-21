#!/usr/bin/env python3
"""Real i386 link -> KMOD -> C validator; generator failures and mutation controls."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_kdebug_imports as imports_gen
import mkmod
import gen_module_schema

SPEC = json.loads((ROOT / 'sdk/kapi.json').read_text())
KAPI = SPEC['version']
LAYOUT = SPEC['generations']['memory_layout']


def command(args):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def vk32():
    image = bytearray(struct.pack('<11I', 0x32334b56, 44, 2, 1,
                                 0x100000, 1, 44, 1, 0, 45, 0) + b'X')
    crc = zlib.crc32(image)
    struct.pack_into('<I', image, 40, crc)
    return bytes(image), crc


def rejected(call, word):
    try:
        call()
    except ValueError as exc:
        assert word in str(exc), str(exc)
    else:
        raise AssertionError('accepted invalid input: ' + word)


def fixture(directory):
    cross = Path(os.environ['CROSS_DIR']) / 'bin'
    entries = imports_gen.load_def()
    imports_gen.generate(entries, directory, stage='S5')
    metadata = json.loads((directory / 'kdebug_imports.json').read_text())
    assert len(metadata['imports']) == 250
    source = directory / 'fixture.S'
    source.write_text('''
.section .text
.globl kdebug_init
.type kdebug_init,@function
kdebug_init:
    .byte 0xb8
.globl absolute_write
absolute_write:
    .long scratch
    call helper
    ret
.globl helper
helper:
    ret
.size kdebug_init, .-kdebug_init
.section .rodata
.globl kdebug_ops
.type kdebug_ops,@object
kdebug_ops:
    .long 3
    .long helper
    .zero 12
    .long kdebug_init
    .zero 56
.size kdebug_ops, .-kdebug_ops
.section .bss
.balign 4
scratch: .zero 8
.section .note.GNU-stack,"",@progbits
''')
    objs = []
    for src in (source, directory / 'kdebug_import_slots.c', directory / 'kdebug_import_stubs.S'):
        obj = src.with_suffix('.o')
        command([cross / 'i386-elf-gcc', '-m32', '-march=i386', '-ffreestanding',
                 '-O2', '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__',
                 *['-I' + str(ROOT / d) for d in ('.', 'include', 'kernel', 'drivers',
                    'exec', 'lib', 'gfx', 'fs', 'kapi', 'arch/i386',
                    'platform/pc98', 'sdk/include/os32')], '-c', src, '-o', obj])
        objs.append(obj)
    elf = directory / 'module.elf'
    command([cross / 'i386-elf-ld', '-m', 'elf_i386', '--emit-relocs',
             '-T', ROOT / 'build/module.ld', *objs, '-o', elf])
    return elf, metadata


def verify(module, elf, metadata, kernel, pair, validator):
    from test_module_image import validate
    image, stats = module.build(elf, metadata, kernel, KAPI, LAYOUT)
    words = struct.unpack_from('<36I', image)
    assert words[:3] == (int.from_bytes(b'KMOD', 'little'), 1, 144)
    assert words[4:6] == (KAPI, LAYOUT) and words[7:10] == (1, 2, 1)
    assert words[33] == 3 and len(module.OPS_NAMES) == 19
    assert words[31] == pair
    checked = bytearray(image)
    struct.pack_into('<I', checked, 128, 0)
    assert zlib.crc32(checked) == words[32]
    reloc = struct.unpack_from('<' + 'I' * words[22], image, words[21])
    # Immediate at byte 1 + two ops pointers + one absolute operand per stub.
    assert len(reloc) == 253 and 1 in reloc and 6 not in reloc
    assert words[24] == 250 and words[26] == 2
    export = [struct.unpack_from('<IHHI', image, words[25] + 12*i) for i in range(2)]
    assert [(e[1], e[3]) for e in export] == [(1, 11), (5, 0)]
    assert stats['prefix_size'] == 144 + 253*4 + 250*12 + 2*12 + words[28]
    assert stats['prefix_alloc'] == words[11]
    expected_imports = [(mkmod.fnv1a(e['name'].encode()), e['type_ver'], e['kind'])
                        for e in metadata['imports']]
    expected_exports = [(1, mkmod.fnv1a(b'selftest_run'), 1),
                        (5, mkmod.fnv1a(b'crt_diag_bootlog'), 1)]
    result = validate(validator, image, kapi=KAPI, layout=LAYOUT,
                      abi=metadata['module_abi_gen'], pair=pair,
                      imports=expected_imports, exports=expected_exports)
    assert result.returncode == 0, result.stdout + result.stderr
    return image, stats


def negative_cases(elf, metadata, kernel):
    def build(data=elf, meta=metadata, vk=kernel, module_id=1):
        return mkmod.build(data, meta, vk, KAPI, LAYOUT, module_id=module_id)
    rejected(lambda: build(module_id=99), 'unknown module ID')
    rejected(lambda: build(data=elf[:40]), 'range')
    bad = bytearray(elf); bad[4] = 2
    rejected(lambda: build(data=bad), 'ELF32')
    parsed = mkmod.Elf(elf)
    text = next(s for s in parsed.sections if s['name'] == '.text')
    shoff = struct.unpack_from('<I', elf, 32)[0]
    text_index = parsed.sections.index(text)
    bad = bytearray(elf)
    struct.pack_into('<I', bad, shoff + text_index*40 + 12, 4096)
    rejected(lambda: build(data=bad), 'VA zero')
    relsec = next(s for s in parsed.sections if s['name'] == '.rel.text')
    bad = bytearray(elf)
    info = struct.unpack_from('<I', bad, relsec['offset'] + 4)[0]
    struct.pack_into('<I', bad, relsec['offset'] + 4, (info & ~255) | 3)
    rejected(lambda: build(data=bad), 'relocation type')
    # Keep a named undefined symbol in an otherwise linked ELF.
    symtab = next(s for s in parsed.sections if s['type'] == 2)
    bad = bytearray(elf)
    symbol_index = next(i for i, s in enumerate(parsed.symtabs[parsed.sections.index(symtab)])
                        if s['name'] == 'kdebug_init')
    struct.pack_into('<H', bad, symtab['offset'] + symbol_index*16 + 14, 0)
    rejected(lambda: build(data=bad), 'unresolved ELF symbol')
    bad = bytearray(elf)
    struct.pack_into('<I', bad, text['offset'] + 1, 0xffffffff)
    rejected(lambda: build(data=bad), 'value outside body')
    bad = bytearray(elf)
    struct.pack_into('<I', bad, relsec['offset'], len(elf))
    rejected(lambda: build(data=bad), 'write outside payload')
    badmeta = json.loads(json.dumps(metadata)); badmeta['imports'][0]['index'] = 1
    rejected(lambda: build(meta=badmeta), 'metadata')
    badmeta = json.loads(json.dumps(metadata))
    badmeta['imports'][1]['name'] = badmeta['imports'][0]['name']
    rejected(lambda: build(meta=badmeta), 'duplicate import name')
    # Distinct ASCII names with the same FNV-1a hash, fixed rather than random.
    assert mkmod.fnv1a(b'costarring') == mkmod.fnv1a(b'liquid')
    badmeta['imports'][0]['name'], badmeta['imports'][1]['name'] = 'costarring', 'liquid'
    rejected(lambda: build(meta=badmeta), 'hash collision')
    ops = parsed.symbol('kdebug_ops')
    ro = next(s for s in parsed.sections if s['name'] == '.rodata')
    bad = bytearray(elf)
    struct.pack_into('<I', bad, ro['offset'] + ops['value'] - ro['addr'], 1)
    rejected(lambda: build(data=bad), 'ops version')
    bad = bytearray(kernel); bad[-1] ^= 1
    rejected(lambda: build(vk=bad), 'CRC mismatch')
    print('mkmod rejects malformed ELF, unsupported relocation, unresolved symbol, '
          'out-of-range relocation, invalid import metadata and corrupt VK32: PASS')


MUTATIONS = (
    ('drop-absolute', 'relocs.append(where)', 'pass'),
    ('keep-pc32', 'if kind == 2:', 'if False:'),
    ('pair-zero', "pair_crc(vmkernel) if policy['required_flags'] & CONSTANTS['FLAG_PAIR'] else 0", '0'),
    ('bad-export-slot', 'fnv1a(encoded), slot, 1, entry', 'fnv1a(encoded), slot - 1, 1, entry'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    assert gen_module_schema.OUTPUT.read_text() == gen_module_schema.render(), 'stale KMOD schema'
    from test_module_image import build_validator
    with tempfile.TemporaryDirectory(prefix='mkmod-') as temp:
        directory = Path(temp)
        validator = build_validator(directory)
        elf_path, metadata = fixture(directory)
        elf = elf_path.read_bytes()
        kernel, pair = vk32()
        image, stats = verify(mkmod, elf, metadata, kernel, pair, validator)
        negative_cases(elf, metadata, kernel)
        imports_gen.check_nm(imports_gen.load_def())
        (directory / 'vmkernel.lz4').write_bytes(kernel)
        cli = [sys.executable, ROOT / 'tools/mkmod.py', '--elf', elf_path,
               '--metadata', directory / 'kdebug_imports.json',
               '--vmkernel', directory / 'vmkernel.lz4', '-o', directory / 'module.bin']
        generations = ['--kapi-abi-gen', str(KAPI), '--layout-gen', str(LAYOUT)]
        output = command([*cli, *generations])
        assert (directory / 'module.bin').read_bytes() == image
        for given in ([], generations[:2], generations[2:]):
            result = subprocess.run([str(a) for a in [*cli, *given]], capture_output=True)
            assert result.returncode == 2 and b'required' in result.stderr
            assert (directory / 'module.bin').read_bytes() == image
        without_pair = cli[:cli.index('--vmkernel')] + cli[cli.index('--vmkernel') + 2:]
        result = subprocess.run([str(a) for a in [*without_pair, *generations]], capture_output=True)
        assert result.returncode == 2 and b'--vmkernel' in result.stderr and b'required' in result.stderr
        assert (directory / 'module.bin').read_bytes() == image
        try:
            mkmod.build(elf, metadata, kernel)
        except TypeError:
            pass
        else:
            raise AssertionError('build() silently supplied generations')
        print('PASS shared schema; CLI requires paired VK32; CLI/API require explicit KAPI and layout generations')
        assert 'prefix=' + str(stats['prefix_size']) + ' B' in output
        print(output.strip())
        print('prefix fixture (250 real generated imports, 2 exports): '
              f"{stats['prefix_size']} B unpadded / {stats['prefix_alloc']} B allocated; "
              'full S5 body relocation/export counts remain unmeasured')
        if args.mutate:
            source = (ROOT / 'tools/mkmod.py').read_text()
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, name
                path = directory / (name + '.py')
                path.write_text(source.replace(old, new))
                spec = importlib.util.spec_from_file_location('mutant', path)
                mutant = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mutant)
                try:
                    verify(mutant, elf, metadata, kernel, pair, validator)
                except (AssertionError, ValueError):
                    print('MUTATE ' + name + ' RED')
                else:
                    raise AssertionError('surviving mutant: ' + name)
    print('mkmod generation -> C validator GREEN; nm -u agrees with .def')
    return 0


if __name__ == '__main__':
    sys.exit(main())
