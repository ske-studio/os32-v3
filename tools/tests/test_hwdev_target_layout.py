#!/usr/bin/env python3
"""H4 target decoding/gates and x86 byte stability; --ppc is manual/PM only."""
CHECK = dict(name='hwdev-target-layout', tier=1, needs=['make:all'], budget=15,
    covers=['tools/tests/test_hwdev_target_layout.py', 'tools/gen_hwdev_contract.py',
            'tools/hwdev_layout.json', 'tools/gen_kdebug_imports.py',
            'tools/module_schema.py', 'tools/build_profile.py', 'build/arch/*.mk',
            'build/kdebug.mk', 'docs/design/hwdev/hwdev_imports.md',
            'kernel/module/kdebug_imports.def', 'sdk/kapi.json', 'tools/mkmod.py', 'hwdev/**', 'kdebug/*.h', 'include/*.h'])
import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import gen_hwdev_contract as generator
from build_profile import query
ROOT = Path(__file__).resolve().parents[2]
PPC_LAYOUT_CRC = 0x17d72be1  # manual/PM-only --ppc pin; x86 pins live in hwdev/generated/


def rejected(call):
    try:
        call()
    except SystemExit:
        return
    raise AssertionError('invalid target/object accepted')


def fixtures(module):
    for target, data, machine, order in [('x86-pc98', 1, 3, '<'),
                                          ('ppc-powermac', 2, 20, '>')]:
        header = bytearray(52)
        header[:6] = b'\x7fELF\x01' + bytes([data])
        header[18:20] = struct.pack(order + 'H', machine)
        actual = module.elf_order(header, target)
        assert module.decode_layout(struct.pack(order + 'II', 0x12345678, 272), 2, actual) == (0x12345678, 272)
        rejected(lambda: module.elf_order(header, 'ppc-powermac' if data == 1 else 'x86-pc98'))
        header[18:20] = struct.pack(order + 'H', 21)
        rejected(lambda: module.elf_order(header, target))
    for header in (b'', b'\x7fELF\x02' + bytes(47), b'\x7fELF\x01\x03' + bytes(46)):
        rejected(lambda: module.elf_order(header, 'x86-pc98'))
    for raw in (b'', bytes(3), bytes(5)):
        rejected(lambda: module.decode_layout(raw, 1, '>'))
    rejected(lambda: module.target_gate('unknown', True))
    rejected(lambda: module.target_gate('ppc-powermac', False))
    module.target_gate('ppc-powermac', True)


def generate(module, out, ppc=False, **options):
    profile = query(ROOT, ['ARCH=ppc', 'PLATFORM=powermac'] if ppc else [], config_only=True)
    cc = profile['CC'][0]
    if not ppc:
        cc = str(Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))/'bin'/cc)
    prefix = Path(cc).parent
    readelf = str(prefix/profile['READELF'][0])
    objcopy = str(prefix/profile['OBJCOPY'][0])
    flags = profile['CFLAGS_MACHINE'] + profile['CFLAGS_BASE']
    module.generate(cc, flags, profile['INC_KERNEL'], out,
                    target='ppc-powermac' if ppc else 'x86-pc98',
                    readelf=readelf, objcopy=objcopy, layout_only=ppc, **options)
    if ppc:
        source = out/'asserts.c'
        source.write_text('#include "' + str((out/'hwdev_contract.h').resolve()) + '"\n')
        subprocess.run([cc, *flags, *profile['INC_KERNEL'], '-fsyntax-only', str(source)], cwd=ROOT, check=True)
        source.unlink()
    return out


def byte_oracle(out):
    expected = ROOT/'hwdev/generated'
    assert sorted(p.name for p in out.iterdir()) == sorted(p.name for p in expected.glob('hwdev_*'))
    for path in expected.glob('hwdev_*'):
        assert (out/path.name).read_bytes() == path.read_bytes(), path.name


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--ppc', action='store_true', help='manual/PM only; never CHECK')
    args = parser.parse_args()
    if args.ppc:
        out = generate(generator, ROOT/'build/out/ppc-powermac/hwdev-layout', True)
        assert sorted(p.name for p in out.iterdir()) == ['hwdev_contract.h', 'hwdev_layout.txt']
        rows = dict((kind+' '+name, int(value)) for kind, name, value in
                    (line.split('\t') for line in (out/'hwdev_layout.txt').read_text().splitlines()))
        expected = {'size struct gf5':1776, 'align struct gf5':8,
                    'offset struct gf5.next_auto':272, 'offset struct gf5.next_step':280,
                    'offset struct gf5.next_sensor':288, 'offset struct gf5.next_toggle':296,
                    'offset struct gf_production.feature5':10936}
        for key, value in expected.items():
            assert rows[key] == value, (key, rows[key], value)
            print(key, rows[key])
        native_enums = [line for line in (ROOT/'hwdev/generated/hwdev_layout.txt').read_text().splitlines() if line.startswith('enum\t')]
        assert native_enums == [line for line in (out/'hwdev_layout.txt').read_text().splitlines() if line.startswith('enum\t')]
        # Pinned after independent review (2026-11-09, order-4 D): PPC layout CRC.
        crc = zlib.crc32((out/'hwdev_layout.txt').read_bytes())
        assert crc == PPC_LAYOUT_CRC, ('PPC layout CRC', hex(crc))
        assert '#define HWDEV_LAYOUT_CRC 0x%08xU' % PPC_LAYOUT_CRC in (out/'hwdev_contract.h').read_text()
        print('PPC layout CRC 0x%08x (pinned)' % crc)
        return
    fixtures(generator)
    with tempfile.TemporaryDirectory(prefix='hwdev-target-') as td:
        byte_oracle(generate(generator, Path(td)/'control'))
        inventory = Path(td)/'wrong-inventory.json'
        inventory.write_text('{"types": {}, "enums": []}\n')
        rejected(lambda: generate(generator, Path(td)/'rejected', inventory=inventory, seed=True))
        assert inventory.read_text() == '{"types": {}, "enums": []}\n'
        assert not (Path(td)/'rejected').exists()
        print('GREEN x86 all generated bytes; synthetic BE/LE, ELF and full-generation gates')
        if args.mutate:
            mutations = [
                ('endian', "return struct.unpack(order + 'I' * count, raw)", "return struct.unpack('<' + 'I' * count, raw)"),
                ('machine', 'if identity != TARGETS[target]:', 'if False:'),
                ('offset', 'values = decode_layout(raw.read_bytes(), len(expressions), order)',
                 'values = tuple(v + 1 for v in decode_layout(raw.read_bytes(), len(expressions), order))'),
                ('CRC', 'lcrc = zlib.crc32(layout.encode()) & 0xffffffff', 'lcrc = 0'),
                ('PPC full', "if target == 'ppc-powermac' and not layout_only:", 'if False:')]
            original = Path(generator.__file__).read_text()
            for name, old, new in mutations:
                assert original.count(old) == 1, name
                namespace = dict(__file__=generator.__file__, __name__='mutant')
                exec(compile(original.replace(old, new), generator.__file__, 'exec'), namespace)
                from types import SimpleNamespace
                module = SimpleNamespace(**namespace)
                try:
                    fixtures(module)
                    byte_oracle(generate(module, Path(td)/name))
                except AssertionError:
                    print('RED', name, '(runtime oracle)')
                else:
                    raise AssertionError('surviving mutation: ' + name)


def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
