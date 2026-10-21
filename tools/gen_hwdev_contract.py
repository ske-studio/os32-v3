#!/usr/bin/env python3
"""Generate the shared i386 HWDEV contract; inventory changes require review."""
import argparse
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile
import zlib
from gen_kdebug_imports import declaration, fnv1a

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / 'docs/tasks/realhw/design/HWDEV_CORE_API.md'
INVENTORY = ROOT / 'tools/hwdev_layout.json'
OUT = ROOT / 'hwdev/generated'


def normalize(text):
    return ''.join(re.findall(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[^\s]', text))


def slots():
    doc = DOC.read_text()
    core = re.findall(r'^\| (\d+) \| `(\w+)` \| `([^`]+)` \|$',
                      doc.split('## 2.', 1)[1].split('## 3.', 1)[0], re.M)
    imports = {e['symbol']: e for e in json.loads((ROOT/'kernel/kdebug_imports.def').read_text())['imports']}
    kernel = re.findall(r'^\| (\d+) \| (\w+) \| (\d+) \|$',
                        doc.split('## 3.', 1)[1].split('## 4.', 1)[0], re.M)
    result = []
    for index, name, source in core:
        result.append(dict(index=int(index), symbol=name, source=source, kind='FUNC', group='core'))
    for index, name, old in kernel:
        e = imports[name]
        if e['index'] != int(old):
            raise SystemExit('kernel declaration index mismatch')
        result.append(dict(e, index=int(index), group='kernel'))
    if [e['index'] for e in result] != list(range(35)):
        raise SystemExit('HWDEV slot order/count')
    for e in result:
        e['declaration'] = declaration(e, ROOT)
    return result


def dwarf_inventory(raw):
    # Read only names and parentage from compiler DWARF, never reconstruct types.
    dies, stack = {}, {}
    current = None
    for line in raw.splitlines():
        m = re.match(r'\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: \d+(?: \((DW_TAG_\w+)\))?', line)
        if m:
            depth, offset, tag = int(m[1]), m[2], m[3]
            current = None
            if tag:
                current = dict(tag=tag, children=[])
                dies[offset] = current
                if depth and depth-1 in stack:
                    stack[depth-1]['children'].append(current)
                stack[depth] = current
            continue
        m = re.search(r'DW_AT_(name|type|byte_size|declaration)\s*:\s*(.*)', line)
        if current is not None and m:
            value = m[2]
            if m[1] == 'name':
                value = value.split('): ', 1)[-1]
            current[m[1]] = value
    aliases = {}
    for d in dies.values():
        if d['tag'] == 'DW_TAG_typedef' and 'type' in d:
            ref = re.search(r'<0x([0-9a-f]+)>', d['type'])
            if ref:
                aliases[ref[1]] = d.get('name')
    types, enums = {}, []
    for offset, d in dies.items():
        if d['tag'] in ('DW_TAG_structure_type', 'DW_TAG_union_type') and 'byte_size' in d:
            name = d.get('name')
            name = (('struct ' if d['tag'] == 'DW_TAG_structure_type' else 'union ') + name) if name else aliases.get(offset)
            if not name:
                raise SystemExit('anonymous aggregate without typedef in shared umbrella')
            members = [c.get('name') for c in d['children'] if c['tag'] == 'DW_TAG_member']
            if None in members:
                raise SystemExit('anonymous member in shared umbrella: ' + name)
            types[name] = sorted(members)
        if d['tag'] == 'DW_TAG_enumeration_type':
            enums += [c['name'] for c in d['children'] if c['tag'] == 'DW_TAG_enumerator']
    return dict(types=dict(sorted(types.items())), enums=sorted(set(enums)))


def generate(cc, flags, includes, out=OUT, inventory=INVENTORY, seed=False):
    entries = slots()
    headers = ['kdebug/recover.h', 'kdebug/recover_gf.h']
    headers += [str(p.relative_to(ROOT)) for p in sorted((ROOT/'kdebug').glob('gfinit*.h'))]
    headers += ['kdebug/kpeek.h', 'include/hwdev_private.h']
    headers += sorted({e['source'] for e in entries if e['group'] == 'kernel'})
    umbrella = ''.join(f'#include "{h}"\n' for h in dict.fromkeys(headers))
    table = 'struct hwdev_core_api {\n    u32 abi, size, contract_crc, layout_crc;\n'
    for e in entries[:25]:
        table += '    ' + e['declaration'].replace(e['symbol'], '(*'+e['symbol']+')', 1) + '\n'
    table += '};\n'
    umbrella += table
    command = [cc, *flags, *includes]
    with tempfile.TemporaryDirectory(prefix='hwdev-contract-') as td:
        td = Path(td)
        src, obj = td/'contract.c', td/'contract.o'
        src.write_text(umbrella)
        subprocess.run([*command, '-g', '-fno-eliminate-unused-debug-types', '-c', str(src), '-o', str(obj)], check=True)
        readelf = str(Path(cc).with_name('i386-elf-readelf'))
        observed = dwarf_inventory(subprocess.check_output([readelf, '--debug-dump=info', str(obj)], text=True))
        if seed:
            inventory.write_text(json.dumps(observed, indent=2)+'\n')
        if observed != json.loads(inventory.read_text()):
            raise SystemExit('HWDEV layout inventory differs from compiler DWARF; update reviewed inventory')
        expressions = []
        for name, members in observed['types'].items():
            expressions += [('size', name, f'sizeof({name})'), ('align', name, f'__alignof__({name})')]
            for member in members:
                expressions += [('offset', name+'.'+member, f'__builtin_offsetof({name}, {member})'),
                                ('member_size', name+'.'+member, f'sizeof((({name} *)0)->{member})')]
        expressions += [('enum', name, name) for name in observed['enums']]
        src.write_text(umbrella+'\nconst u32 values[] __attribute__((section(".hwdev_layout"))) = {\n'+
                       ',\n'.join('(u32)('+e[2]+')' for e in expressions)+'\n};\n')
        subprocess.run([*command, '-c', str(src), '-o', str(obj)], check=True)
        raw = td/'layout.bin'
        subprocess.run([str(Path(cc).with_name('i386-elf-objcopy')),
                        '--dump-section', '.hwdev_layout='+str(raw), str(obj)], check=True)
        values = struct.unpack('<'+'I'*len(expressions), raw.read_bytes())
        layout = ''.join(f'{kind}\t{name}\t{value}\n' for (kind,name,_),value in zip(expressions,values))
        preprocessed = subprocess.check_output([*command, '-E', '-P', str(src)], text=True)
        # The layout probe is not a shared declaration.
        preprocessed = preprocessed.split('const u32 values[]', 1)[0]
        contract = ''.join(f'{e["group"]}:{e["index"]}:{e["symbol"]}:{normalize(e["declaration"])}\n' for e in entries)+normalize(preprocessed)
    out.mkdir(parents=True, exist_ok=True)
    crc = zlib.crc32(contract.encode()) & 0xffffffff
    lcrc = zlib.crc32(layout.encode()) & 0xffffffff
    header = '/* Generated by tools/gen_hwdev_contract.py; do not edit. */\n#ifndef HWDEV_CONTRACT_H\n#define HWDEV_CONTRACT_H\n'+umbrella
    header += f'#define HWDEV_CONTRACT_CRC 0x{crc:08x}U\n#define HWDEV_LAYOUT_CRC 0x{lcrc:08x}U\n#define HWDEV_IMPORT_COUNT 35U\n#define HWDEV_CORE_SLOTS 25U\n'
    for i, ((_,_,expr),value) in enumerate(zip(expressions,values)):
        header += f'STATIC_ASSERT((u32)({expr}) == {value}U, hwdev_layout_{i});\n'
    header += 'const struct hwdev_core_api *hwdev_core_get(void);\n#endif\n'
    files = {'hwdev_contract.h':header, 'hwdev_layout.txt':layout}
    files['hwdev_core.c'] = '#include "hwdev_contract.h"\nstatic const struct hwdev_core_api core = {\n    HWDEV_ABI, sizeof(core), HWDEV_CONTRACT_CRC, HWDEV_LAYOUT_CRC,\n'+''.join('    '+e['symbol']+',\n' for e in entries[:25])+'};\nconst struct hwdev_core_api *hwdev_core_get(void) { return &core; }\n'
    imports = ['#include "hwdev_contract.h"', '#include "kernel/module_image.h"',
               'static const struct module_symbol_contract hwdev_import_contracts[] = {']
    imports += [f'    {{0x{fnv1a(e["symbol"].encode()):08x}U, 1, 1}},' for e in entries]
    imports += ['};', 'static const struct module_symbol_contract hwdev_export_contracts[] = {',
                f'    {{0x{fnv1a(b"initialize"):08x}U, 1, 0}},', '};',
                'static void hwdev_fill_imports(u32 *slots, const struct hwdev_core_api *core) {']
    imports += [f'    slots[{e["index"]}] = (u32)(uptr)'+('core->' if e['group']=='core' else '')+e['symbol']+';' for e in entries]
    imports += ['}']
    files['hwdev_imports.h'] = '\n'.join(imports)+'\n'
    for name, text in files.items():
        p = out/name
        if not p.exists() or p.read_text() != text:
            p.write_text(text)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    # Compiler, flags and includes come from build/kdebug.mk (KERNEL_CFLAGS, INC_KERNEL).
    p.add_argument('--cc', required=True)
    p.add_argument('--flags', required=True)
    p.add_argument('--includes', required=True)
    p.add_argument('--out', type=Path, default=OUT)
    p.add_argument('--seed-inventory', action='store_true')
    p.add_argument('--check', action='store_true')
    a = p.parse_args()
    if a.check:
        with tempfile.TemporaryDirectory(prefix='hwdev-reproduce-') as td:
            out = Path(td)
            generate(a.cc, shlex.split(a.flags), shlex.split(a.includes), out)
            for path in out.iterdir():
                if path.read_bytes() != (a.out/path.name).read_bytes():
                    raise SystemExit('stale HWDEV contract: ' + path.name)
        print('PASS HWDEV generated contract/layout/asserts reproduce exactly')
    else:
        generate(a.cc, shlex.split(a.flags), shlex.split(a.includes), a.out, seed=a.seed_inventory)

if __name__ == '__main__':
    main()
