#!/usr/bin/env python3
"""Generate the private, positional kdebug import ABI (never part of KAPI).

The JSON .def fixes order, stage and context permissions. Existing signatures
are extracted from their declaring headers; four legacy local externs use the
original C source. OP declarations are new contracts owned by this definition.
Generated files belong in a build directory, not in the source tree.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from module_schema import CONSTANTS, SYMBOL_KINDS

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DEF = ROOT / 'kernel/kdebug_imports.def'
KINDS = SYMBOL_KINDS
STAGE_KINDS = {
    'S1': ('FUNC',),
    'S2': ('FUNC',) * 129 + ('GETTER',) * 22 +
          ('GETTER', 'SETTER') * 12 + ('PTR',) * 7 + ('OP',) * 6,
    'S3': ('FUNC',) * 14 + ('GETTER',) * 2 + ('PTR',) * 4,
    'S4': ('FUNC',) * 25,
    'S5': ('FUNC',) * 30,
    # S3e: all five moved selftests.
    'S6': ('FUNC',) * 38 + ('GETTER',) + ('SETTER',) * 2 + ('PTR',) * 3 + ('FUNC',),
}
STAGES = {}
for _stage, _kinds in STAGE_KINDS.items():
    STAGES[_stage] = (list(STAGES.values())[-1] if STAGES else 0) + len(_kinds)
LAST_STAGE = list(STAGE_KINDS)[-1]
GENERATIONS = json.loads((ROOT / 'sdk/kapi.json').read_text())['generations']
# Identify contracts by symbol: S3b..S3e compact the old selftest slots.
GF_ONLY_SYMBOLS = frozenset(('pci_cfg_read32', 'pci_cfg_read16',
                             'pci_cfg_write32', 'pci_cfg_write16', 'gf_production_selected'))
# Raw function addresses do not create an nm -u reference to the function.
# This manifest also fixes which moved object may consume each address.
ADDRESS_IMPORTS = {'kapi_reserved_nosys': 'selftest_exec',
                   'shlib_read_page': 'selftest_lease'}
SELFTEST_OBJECTS = ('selftest_con_sink', 'selftest_kbd_inject',
                    'selftest_lease', 'selftest_exec')
ABI_SYMBOL = '__os32_abi_{kapi_abi}_mem_{memory_layout}_fmt_{os32x_format}'.format(**GENERATIONS)


def hwdev_gf_objects(root=ROOT):
    """Read the GF object inventory from its authoritative make definition."""
    text = (Path(root) / 'build/hwdev.mk').read_text()
    match = re.search(r'^HWDEV_GF := ([\w ]+)$', text, re.M)
    if not match:
        raise ValueError('missing HWDEV_GF literal inventory')
    names = tuple(match[1].split())
    # P6 links the common runner and opcode implementations into HWDEV.
    names += ('gf/runner', 'gf/save', 'gf/pll') + tuple(
        'gf/op/' + p.stem for p in sorted((Path(root)/'hwdev/gf/op').glob('*.c')))
    if not names or len(names) != len(set(names)):
        raise ValueError('empty/duplicate HWDEV_GF inventory')
    return names


def fnv1a(data):
    value = CONSTANTS['NAME_HASH_BASIS']
    for byte in data:
        value = ((value ^ byte) * CONSTANTS['NAME_HASH_PRIME']) & 0xffffffff
    return value


def clean_source(text):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    return re.sub(r'^\s*#.*$', '', text, flags=re.M)


def declaration(row, root):
    name = row['symbol']
    if row['kind'] == 'OP':
        return row['declaration']
    text = clean_source((root / row['source']).read_text())
    if row['kind'] == 'FUNC':
        match = re.search(r'(?m)^\s*((?:extern\s+)?[\w \t*]+\b' +
                          re.escape(name) + r'\s*\([^;{}]*\)[^;{}]*;)', text)
        if not match:
            raise ValueError(f'no function declaration: {name}')
        return re.sub(r'\s+', ' ', match[1]).strip().removeprefix('extern ')
    for match in re.finditer(r'\bextern\s+([^;{}]+);', text):
        # Data declarations may share a type ("extern u32 a, b;").
        parts = match[1].split(',')
        first = re.fullmatch(r'(.+?)([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*', parts[0])
        if not first:
            continue
        dtype = first[1].strip()
        variables = [first[2] + (first[3] or '')] + parts[1:]
        for variable in variables:
            m = re.fullmatch(r'\s*' + re.escape(name) + r'\s*(\[[^\]]*\])?\s*', variable)
            if m:
                return f'{dtype} {name}{m[1] or ""};'
    raise ValueError(f'no data declaration: {name}')


def load_def(path=DEFAULT_DEF, root=ROOT):
    document = json.loads(Path(path).read_text())
    rows = document['imports']
    if document['schema'] != 2 or len(rows) != STAGES[LAST_STAGE]:
        raise ValueError('expected schema 2 and complete stage table')
    contracts = [(stage, kind) for stage, kinds in STAGE_KINDS.items() for kind in kinds]
    entries = []
    names = set()
    for index, row in enumerate(rows):
        kind = row['kind']
        expected_stage, expected_kind = contracts[index]
        if row['index'] != index or row['stage'] != expected_stage or kind != expected_kind:
            raise ValueError(f'invalid index/stage/kind at {index}')
        symbol = row['symbol']
        if not re.fullmatch(r'[A-Za-z_]\w*', symbol):
            raise ValueError('invalid symbol')
        name = symbol if kind in ('FUNC', 'OP') else 'kdi_' + {'GETTER': 'get_', 'SETTER': 'set_', 'PTR': 'ptr_'}[kind] + symbol
        if name in names:
            raise ValueError(f'duplicate import: {name}')
        names.add(name)
        decl = declaration(row, Path(root))
        dtype = None
        array = False
        if kind not in ('FUNC', 'OP'):
            m = re.fullmatch(r'(.+?)\s+' + re.escape(symbol) + r'(\[[^\]]*\])?;', decl)
            if not m:
                raise ValueError(f'unsupported data declaration: {decl}')
            dtype = re.sub(r'\bvolatile\s+', '', m[1])
            array = bool(m[2])
            if kind == 'PTR':
                signature = f'{dtype} *{name}(void);'
            elif kind == 'GETTER':
                signature = f'{dtype} {name}(void);'
            else:
                signature = f'void {name}({dtype} value);'
        else:
            signature = decl
        entries.append(dict(row, name=name, kind_name=kind, kind=KINDS[kind],
                            type_ver=1, signature=signature, source_declaration=decl,
                            dtype=dtype, array=array))
    return entries


def abi_hash(entries):
    # Canonical complete resolved definition: names, actual types, order and flags.
    return fnv1a((json.dumps(entries, sort_keys=True, separators=(',', ':')) + '\n').encode())


def symbols(nm, paths, undefined):
    result = set()
    for path in paths:
        args = [nm, '-u' if undefined else '--defined-only', str(path)]
        for line in subprocess.check_output(args, text=True).splitlines():
            fields = line.split()
            if len(fields) >= 2:
                result.add(fields[-1])
    return result


def address_references(entries, callers, root):
    """Account for raw slots in linked moved sources, not stale output files."""
    names = {e['name'] for e in entries if e['kind_name'] == 'FUNC'}
    refs = set()
    for path in callers:
        if path.stem not in SELFTEST_OBJECTS:
            continue
        source = clean_source((root / 'kdebug' / (path.stem + '.c')).read_text())
        used = set(re.findall(r'\bKDI_ADDR_(\w+)\b', source))
        for name in used:
            if ADDRESS_IMPORTS.get(name) != path.stem or name not in names:
                raise ValueError('invalid address import: ' + name)
        required = {name for name, stem in ADDRESS_IMPORTS.items()
                    if stem == path.stem and name in names}
        if required - used:
            raise ValueError('missing address import: ' + str(sorted(required - used)))
        refs |= used
    return refs


def check_nm(entries, nm=None, root=ROOT):
    nm = nm or str(Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-nm')
    root = Path(root)
    map_text = (root / 'build/out/kdebug/kdebug.map').read_text()
    linked = [root / p for p in re.findall(r'^LOAD (.+\.o)$', map_text, re.M)]
    moved = [p for p in linked if p.stem in SELFTEST_OBJECTS]
    groups = {'S1': ['crt_diag'], 'S2': ['kselftest'], 'S3': ['v86_gcap', 'v86_gcap_math']}
    cumulative = set()
    for stage, stems in groups.items():
        paths = ([root / 'build/out/kdebug/crt_diag.o'] if stage == 'S1' else
                 [root / 'build/out/kdebug/kselftest.o', root / 'kernel/kaudit.o'] if stage == 'S2' else
                 [root / 'build/out/kdebug' / (name + '.o') for name in stems])
        if stage == 'S2':
            paths += moved
        missing = [str(p) for p in paths if not p.exists()]
        if missing:
            raise SystemExit('gen_kdebug_imports: run make kernel first (missing: ' + ', '.join(missing) + ')')
        refs = symbols(nm, paths, True) - symbols(nm, paths, False)
        # Linker contracts are not imports: module.ld defines the ABI marker;
        # __bss_end belongs to the retained kernel SHM boot observation.
        refs.discard(ABI_SYMBOL)
        if stage == 'S2':
            raw_addresses = address_references(entries, moved, root)
            if raw_addresses:
                refs.discard('kdebug_import_slots')
                refs |= raw_addresses
        if stage == 'S2':
            refs.discard('__bss_end')
            refs.discard('__kernel_resident_end')
            # The module now uses accessors; the retained audit accounts for
            # the original audit-only references. Keep the S0 inventory fixed.
            module_refs = symbols(nm, paths[:1], True)
            operations = {e['name'] for e in entries[:STAGES[stage]] if e['kind_name'] == 'OP'}
            if not operations <= module_refs:
                raise ValueError('S2 missing operation references: ' + str(sorted(operations-module_refs)))
            refs = {next((e['symbol'] for e in entries if e['kind_name'] in ('GETTER', 'SETTER', 'PTR') and e['name'] == name), name)
                    for name in refs - operations}
        if stage in ('S1', 'S3'):
            refs = {next((e['symbol'] for e in entries if e['name'] == name), name) for name in refs}
        # Later-stage imports may be used by the original units and the
        # moved selftests; stage boundaries follow the current compact table.
        later = {e['symbol'] for e in entries[STAGES[stage]:]}
        cumulative |= refs - later
        expected = {e['symbol'] for e in entries[:STAGES[stage]] if e['kind_name'] != 'OP'}
        # Retained ABI slots need not have current module callers, but their
        # implementation must still exist in the linked kernel export table.
        retained = {e['symbol'] for e in entries[:STAGES[stage]] if e.get('retained')}
        missing = expected - cumulative - retained
        if missing or cumulative - expected:
            raise ValueError(f'{stage} nm mismatch: missing={sorted(missing)}, unexpected={sorted(cumulative-expected)}')
    # M3c: read actual linked inputs, never stale GF .o files left in the
    # output directory. Their old kernel slots remain callable ABI entries.
    paths = linked
    if any(p.stem in {Path(name).name for name in hwdev_gf_objects(root)} for p in paths):
        raise ValueError('GF object still linked into kdebug')
    callers = [p for p in paths if p.stem not in ('kdebug_import_slots', 'kdebug_import_stubs')]
    refs = symbols(nm, callers, True) - symbols(nm, callers, False) - {ABI_SYMBOL}
    raw_addresses = address_references(entries, callers, root)
    if raw_addresses:
        refs.discard('kdebug_import_slots')
        refs |= raw_addresses
    expected = {e['name'] for e in entries}
    if refs - expected:
        raise ValueError('unexpected core references: ' + str(sorted(refs - expected)))
    for e in entries:
        if e['symbol'] in GF_ONLY_SYMBOLS:
            if not e.get('retained') or e['name'] in refs:
                raise ValueError('GF-only slot must be retained without core callers: ' + e['name'])
    # S6 includes data accessors; compare generated names, retaining kind.
    missing_s6 = {e['name'] for e in entries if e['stage'] == 'S6' and not e.get('retained')} - refs
    if missing_s6:
        raise ValueError('S6 nm mismatch: missing=' + str(sorted(missing_s6)))
    retained = {e['symbol'] for e in entries if e.get('retained')}
    absent = retained - symbols(nm, [root / 'build/out/kernel.elf'], False)
    if absent:
        raise ValueError('retained imports missing from kernel: ' + str(sorted(absent)))
    return True


def generate(entries, out_dir, stage=LAST_STAGE):
    selected = entries[:STAGES[stage]]
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    headers = sorted({e['source'] for e in selected if e.get('source', '').endswith('.h')})
    common = ['/* Generated by gen_kdebug_imports.py; do not edit. */', '#include "types.h"']
    common += [f'#include "{h}"' for h in headers]
    common += sorted({e['source_declaration'] if e['kind_name'] == 'FUNC' else 'extern ' + e['source_declaration'] for e in selected if e.get('source', '').endswith('.c')})
    declarations = [e['signature'] for e in selected if e['kind_name'] != 'FUNC']
    header = common + declarations
    header += [f'#define KDEBUG_IMPORT_COUNT {len(selected)}', f'#define KDEBUG_ABI_GEN 0x{abi_hash(selected):08x}u']
    header += ['#include "kernel/module_image.h"',
               'extern void (*const kdebug_kexports[])(void);',
               'extern const struct module_symbol_contract kdebug_import_contracts[];',
               'extern const struct module_symbol_contract kdebug_export_contracts[MODULE_OPS_SLOTS];']
    header += ['extern unsigned long kdebug_import_slots[KDEBUG_IMPORT_COUNT];']
    (out / 'kdebug_imports.h').write_text('#ifndef KDEBUG_IMPORTS_GENERATED_H\n#define KDEBUG_IMPORTS_GENERATED_H\n' + '\n'.join(header) + '\n#endif\n')
    kernel = ['#include "kdebug_imports.h"']
    macros = ['/* Include after original headers; assignments use explicit setters. */', '#include "kdebug_imports.h"']
    for e in selected:
        kind, name, symbol = e['kind_name'], e['name'], e['symbol']
        if kind == 'FUNC' and name in ADDRESS_IMPORTS:
            macros += [f'#define KDI_INDEX_{name} {e["index"]}',
                       f'#define KDI_ADDR_{name} ((__typeof__(&{name}))(uptr)kdebug_import_slots[KDI_INDEX_{name}])']
        if kind in ('GETTER', 'SETTER', 'PTR'):
            body = f'{symbol} = value;' if kind == 'SETTER' else f'return {"&" if kind == "PTR" and not e["array"] else ""}{symbol};'
            kernel.append(e['signature'][:-1] + ' { ' + body + ' }')
            if kind != 'SETTER':
                call = ('*' if kind == 'PTR' and not e['array'] else '') + name + '()'
                macros.append(f'#define {symbol} ({call})')
    macros += ['#define ksel_pass (kdi_get_kselftest_pass())', '#define ksel_fail (kdi_get_kselftest_fail())'] if stage != 'S1' else []
    kernel += ['void (*const kdebug_kexports[])(void) = {']
    kernel += [f'    (void (*)(void)){e["name"]},' for e in selected]
    kernel += ['};', 'const unsigned long kdebug_abi_gen = KDEBUG_ABI_GEN;', 'const unsigned long kdebug_kexport_count = KDEBUG_IMPORT_COUNT;']
    kernel += ['const struct module_symbol_contract kdebug_import_contracts[] = {']
    kernel += [f'    {{0x{fnv1a(e["name"].encode()):08x}u, {e["type_ver"]}, {e["kind"]}}},' for e in selected]
    kernel += ['};', 'const struct module_symbol_contract kdebug_export_contracts[MODULE_OPS_SLOTS] = {']
    from mkmod import OPS_NAMES
    slots = {'S1': [5], 'S2': list(range(1, 6)),
             'S3': list(range(1, 17)), 'S4': list(range(1, 17)) + [18],
             'S5': list(range(1, 17)) + [18, 19, 20],
             'S6': list(range(1, 17)) + [18, 19, 20]}[stage]
    kernel += [f'    [{slot - 1}] = {{0x{fnv1a(OPS_NAMES[slot - 1].encode()):08x}u, 1, 0}},' for slot in slots]
    kernel += ['};']
    (out / 'kdebug_kexports.c').write_text('\n'.join(kernel) + '\n')
    (out / 'kdebug_import_macros.h').write_text('\n'.join(macros) + '\n')
    (out / 'kdebug_import_slots.c').write_text('#include "kdebug_imports.h"\nunsigned long kdebug_import_slots[KDEBUG_IMPORT_COUNT]\n__attribute__((section(".data.kdebug_import_slots"), aligned(4))) = {0};\n')
    asm = ['/* Generated i386 cdecl tail-jumps preserve variadic calls. */', '.section .text.kdebug_imports,"ax",@progbits', '.code32']
    for e in selected:
        asm += [f'.globl {e["name"]}', f'.type {e["name"]}, @function', f'{e["name"]}:', f'    jmp *kdebug_import_slots+{4*e["index"]}', f'.size {e["name"]}, .-{e["name"]}']
    asm += ['.section .note.GNU-stack,"",@progbits']
    (out / 'kdebug_import_stubs.S').write_text('\n'.join(asm) + '\n')
    metadata = {'module_abi_gen': abi_hash(selected), 'stage': stage, 'count': len(selected), 'imports': selected}
    (out / 'kdebug_imports.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--def', dest='definition', type=Path, default=DEFAULT_DEF)
    parser.add_argument('--out-dir', type=Path)
    parser.add_argument('--stage', choices=STAGES, default=LAST_STAGE)
    parser.add_argument('--check-nm', action='store_true')
    parser.add_argument('--nm')
    args = parser.parse_args()
    try:
        entries = load_def(args.definition)
        if args.check_nm:
            check_nm(entries, args.nm)
            print('kdebug imports: actual nm -u sets match ' +
                  ' '.join(f'{stage}={count}' for stage, count in STAGES.items()) +
                  ' (linked core, data accessors and address manifest)')
        if args.out_dir:
            generate(entries, args.out_dir, args.stage)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as exc:
        print(f'gen_kdebug_imports: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
