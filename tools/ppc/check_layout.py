#!/usr/bin/env python3
"""NOTEST: manual/PM PowerPC type, record and HWDEV layout acceptance.

No CHECK or DEFERRED registration: real PPC tools are required. The type/record
--ppc entries are reused; HWDEV acceptance and its pin live here.
Syntax/object observations do not establish BE runtime or module support.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_profile import query
import gen_hwdev_contract as generator
from gen_hwdev_contract import decode_layout, elf_order

ROOT = Path(__file__).resolve().parents[2]
# Independently reviewed order-4 D observation, never an x86 pin.
PPC_LAYOUT_CRC = 0x17d72be1
PROBES = (
    ('type-contract', 'tools/tests/test_type_contract.py'),
    ('record-layout', 'tools/tests/test_record_layout.py'),
)


def type_values(tools, profile, env):
    """Read compiler-evaluated constants; do not infer layout from Python types."""
    expressions = {
        'char_bits': '__CHAR_BIT__', 'char_signed': '(char)-1 < 0',
        **{name + '_size': 'sizeof(' + name + ')' for name in
           ('short', 'int', 'long', 'u8', 'i8', 'u16', 'i16', 'u32', 'i32', 'uptr')},
        'pointer_size': 'sizeof(void *)',
        'enum_size': 'sizeof(enum type_contract_enum)',
        'u64_size': 'sizeof(unsigned long long)',
        'u64_member_align': '__builtin_offsetof(struct type_contract_u64,n)',
        'u16_member_align': '__builtin_offsetof(struct type_contract_u16,n)',
        'u32_member_align': '__builtin_offsetof(struct type_contract_u32,n)',
    }
    with tempfile.TemporaryDirectory(prefix='ppc-type-values-') as td:
        directory = Path(td)
        source, obj, raw = (directory / name for name in ('types.c', 'types.o', 'types.bin'))
        source.write_text('#include "type_contract.h"\n'
            'const unsigned int values[] __attribute__((section(".layout"))) = {' +
            ','.join(expressions.values()) + '};\n')
        argv = [tools['gcc'], *profile['CFLAGS_MACHINE'], *profile['CFLAGS_BASE'],
                '-Iarch/ppc', '-Iinclude', '-MF', str(directory / 'types.d'),
                '-c', str(source), '-o', str(obj)]
        subprocess.run(argv, cwd=ROOT, env=env, check=True, capture_output=True, text=True)
        order = elf_order(obj.read_bytes(), 'ppc-powermac')
        subprocess.run([tools['objcopy'], '-O', 'binary', '--only-section=.layout',
                        str(obj), str(raw)], env=env, check=True, capture_output=True, text=True)
        return dict(zip(expressions, decode_layout(raw.read_bytes(), len(expressions), order)))


def hwdev_values(tools, profile, env):
    out = ROOT / 'build/out/ppc-powermac/hwdev-layout'
    flags = profile['CFLAGS_MACHINE'] + profile['CFLAGS_BASE']
    generator.generate(tools['gcc'], flags, profile['INC_KERNEL'], out,
                       target='ppc-powermac', readelf=tools['readelf'],
                       objcopy=tools['objcopy'], layout_only=True)
    assert sorted(p.name for p in out.iterdir()) == ['hwdev_contract.h', 'hwdev_layout.txt']
    with tempfile.TemporaryDirectory(prefix='ppc-hwdev-asserts-') as td:
        source = Path(td) / 'asserts.c'
        source.write_text('#include "' + str(out / 'hwdev_contract.h') + '"\n')
        subprocess.run([tools['gcc'], *flags, *profile['INC_KERNEL'],
                        '-MF', str(Path(td) / 'asserts.d'), '-fsyntax-only', str(source)],
                       cwd=ROOT, env=env, check=True, capture_output=True, text=True)
    layout = (out / 'hwdev_layout.txt').read_text()
    rows = {kind + ' ' + name: int(value) for kind, name, value in
            (line.split('\t') for line in layout.splitlines())}
    expected = {'size struct gf5': 1776, 'align struct gf5': 8,
                'offset struct gf5.next_auto': 272, 'offset struct gf5.next_step': 280,
                'offset struct gf5.next_sensor': 288, 'offset struct gf5.next_toggle': 296,
                'offset struct gf_production.feature5': 10936}
    for key, value in expected.items():
        assert rows[key] == value, (key, rows[key], value)
    native = (ROOT / 'hwdev/generated/hwdev_layout.txt').read_text()
    assert [line for line in native.splitlines() if line.startswith('enum\t')] == [
        line for line in layout.splitlines() if line.startswith('enum\t')]
    crc = zlib.crc32((out / 'hwdev_layout.txt').read_bytes())
    assert crc == PPC_LAYOUT_CRC, ('PPC layout CRC', hex(crc))
    assert '#define HWDEV_LAYOUT_CRC 0x%08xU' % crc in (out / 'hwdev_contract.h').read_text()
    return rows, crc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', action='store_true', help='JSON is also the default')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')).resolve()
    report = dict(target='ppc-powermac', cc=[str(cross / 'bin/powerpc-elf-gcc')],
                  units=[], accept_errors=[])
    try:
        tools = {name: str(cross / ('bin/powerpc-elf-' + name))
                 for name in ('gcc', 'readelf', 'objcopy')}
        report['tools'] = tools
        for name, path in tools.items():
            if not os.access(path, os.X_OK):
                raise ValueError('required PowerPC tool unavailable: ' + path)
        env = dict(os.environ, CROSS_DIR=str(cross), PPC_CROSS_DIR=str(cross))
        # Do not inherit caller make overrides into the PM observation.
        for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'GNUMAKEFLAGS'):
            env.pop(name, None)
        report['tool_versions'] = {
            name: subprocess.check_output([path, '--version'], text=True, env=env).splitlines()[0]
            for name, path in tools.items()}
        profile = query(ROOT, ['ARCH=ppc', 'PLATFORM=powermac',
                               'PPC_CROSS_DIR=' + str(cross)], config_only=True)
        report['flags'] = {key: profile[key] for key in
                           ('CFLAGS_MACHINE', 'CFLAGS_BASE', 'INC_KERNEL')}
        for name, source in PROBES:
            argv = [sys.executable, '-B', source, '--ppc']
            result = subprocess.run(argv, cwd=ROOT, env=env, text=True, capture_output=True)
            report['units'].append(dict(source=source, argv=argv,
                returncode=result.returncode,
                status='failed' if result.returncode else 'success',
                observation=result.stdout, error=result.stderr))
            if result.returncode:
                report['accept_errors'].append(name + ' failed')
        if not report['accept_errors']:
            unit = dict(source='tools/ppc/check_layout.py', probe='hwdev-layout',
                        status='failed', returncode=1, error='')
            report['units'].append(unit)
            rows, crc = hwdev_values(tools, profile, env)
            report['hwdev_layout'] = rows
            report['layout_crc'] = '0x%08x' % crc
            unit.update(status='success', returncode=0)
            report['types'] = type_values(tools, profile, env)
            report['limitations'] = 'compile/object layout only; BE runtime and PPC modules unverified'
    except (OSError, ValueError, AssertionError, SystemExit, subprocess.CalledProcessError) as exc:
        report['accept_errors'].append(str(exc))
        if report['units'] and report['units'][-1]['status'] == 'failed':
            report['units'][-1]['error'] = str(exc)
    text = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')
    return bool(report['accept_errors'])


if __name__ == '__main__':
    raise SystemExit(main())
