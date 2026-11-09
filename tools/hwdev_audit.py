#!/usr/bin/env python3
"""Measure the linked HWDEV image and reconcile GF references with CORE_API.

No runtime ABI is emitted. The JSON report carries declarations, slot order,
object users, ELF/header sizes and map provenance for the later generators.
"""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess

from gen_kdebug_imports import ABI_SYMBOL, declaration, symbols, hwdev_gf_objects, hwdev_inventory
from mkmod import Elf
from module_schema import COMPONENTS, HEADER_FIELDS

ROOT = Path(__file__).resolve().parents[1]
DOC = 'docs/design/hwdev/hwdev_imports.md'
# M5 keeps these slots in the shared diagnostic contract. Their production
# caller lives in GeForce, rather than the HWDEV diagnostic object inventory.
GEFORCE_RETAINED_IMPORTS = frozenset((
    'geforce_resource_lookup', 'paging_device_map', 'paging_device_unmap'))


def require(ok, message):
    if not ok:
        raise SystemExit(message)


def linked_accounting(binary, elf):
    header = dict(zip(HEADER_FIELDS, struct.unpack_from('<36I', binary)))
    sections = {s['name']: s['size'] for s in elf.sections if s['flags'] & 2}
    policy = next(c for c in COMPONENTS if c['name'] == 'HWDEV')
    payload, mem_size, _ = elf.body(policy)
    # Input .rodata strings can be merged by ld (SHF_MERGE), so raw object
    # sizes are attribution only. The linked bytes own payload accounting.
    require(header['IMAGE_SIZE'] == len(binary) and
            header['PAYLOAD_OFF'] + header['PAYLOAD_SIZE'] == len(binary) and
            header['PAYLOAD_SIZE'] == len(payload) and header['MEM_SIZE'] == mem_size and
            binary[header['PAYLOAD_OFF']:] == payload, 'image/section accounting mismatch')
    content = sum(sections.get('.' + k, 0) for k in ('text', 'rodata', 'data'))
    return header, sections, len(payload) - content


def audit(out, cross):
    map_path = out / 'hwdev/hwdev.map'
    map_text = map_path.read_text()
    objects = [Path(p) for p in re.findall(r'^LOAD (.+\.o)$', map_text, re.M)]
    require(objects and len(objects) == len(set(objects)), 'missing/duplicate map inputs')
    gf_names = hwdev_gf_objects() + hwdev_inventory('HWDEV_SHARED')
    require(len(gf_names) == len(set(gf_names)), 'duplicate GF object inventory')
    def object_name(p):
        return p.resolve().relative_to((out/'hwdev').resolve()).with_suffix('').as_posix()
    gf = [p for p in objects if object_name(p) in gf_names]
    require({object_name(p) for p in gf} == set(gf_names), 'GF object inventory mismatch')
    nm = str(cross / 'bin/i386-elf-nm')
    refs = symbols(nm, gf, True) - symbols(nm, gf, False) - {ABI_SYMBOL}
    doc = (ROOT / DOC).read_text()
    core_table = doc.split('## 2.', 1)[1].split('## 3.', 1)[0]
    kernel_table = doc.split('## 3.', 1)[1].split('## 4.', 1)[0]
    rows = re.findall(r'^\| (\d+) \| `(\w+)` \| `([^`]+)` \|$',
                      core_table, re.M)
    private = json.loads((ROOT/'hwdev/hwdev_imports.def').read_text())['imports']
    require([int(r[0]) for r in rows] == [e['index'] for e in private if e['group'] == 'core'],
            'core slot order/count')
    imports = {r['symbol']: r for r in json.loads((ROOT/'kernel/module/kdebug_imports.def').read_text())['imports']}
    core_names = {r[1] for r in rows}
    kernel_names = refs & imports.keys()
    require(refs == core_names | kernel_names, f'missing={refs-core_names-kernel_names}, unused={core_names-refs}')
    kernel_rows = re.findall(r'^\| (\d+) \| (\w+) \| (\d+) \|$',
                             kernel_table, re.M)
    # The reviewed declaration list owns the private slot order, which differs
    # from kdebug index order. Reconcile it with both object users and the docs.
    private_kernel = [e for e in private if e['group'] == 'kernel']
    kernel_names |= GEFORCE_RETAINED_IMPORTS
    require(len(kernel_names) == len(private_kernel), 'kernel import table count')
    require({e['symbol'] for e in private_kernel} == kernel_names,
            'private kernel import slots/symbols')
    expected_kernel = [(str(e['index']), e['symbol'], str(imports[e['symbol']]['index']))
                       for e in private_kernel]
    require(kernel_rows == expected_kernel, 'kernel import table order/symbol/index')
    slots = []
    for index, name, source in rows:
        slots.append(dict(index=int(index), symbol=name, declaration=declaration(
            dict(symbol=name, source=source, kind='FUNC'), ROOT),
            users=[p.name for p in gf if name in symbols(nm, [p], True)]))
    kernel = [dict(symbol=n, kdebug_index=imports[n]['index'], declaration=declaration(imports[n], ROOT))
              for n in sorted(kernel_names, key=lambda n: imports[n]['index'])]
    groups = {g: dict(text=0, rodata=0, data=0, bss=0) for g in ('module', 'gf')}
    details = {}
    for p in objects:
        sizes = dict(text=0, rodata=0, data=0, bss=0)
        raw = subprocess.check_output([str(cross/'bin/i386-elf-size'), '-A', str(p)], text=True)
        for name, size in re.findall(r'^(\.[\w.]+)\s+(\d+)\s+\d+', raw, re.M):
            key = name.split('.')[1]
            if key in sizes:
                sizes[key] += int(size)
        details[p.name] = sizes
        group = groups['gf' if p in gf else 'module']
        for key, value in sizes.items():
            group[key] += value
    binary = (out/'hwdev.mod').read_bytes()
    elf = Elf((out/'hwdev/hwdev.elf').read_bytes())
    header, sections, padding = linked_accounting(binary, elf)
    return dict(header=header, elf_sections=sections, objects=details, groups=groups,
                file_other=dict(prefix=header['PAYLOAD_OFF'], padding=padding),
                map=str(map_path), core_slots=slots, kernel_imports=kernel)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('build/out'))
    parser.add_argument('--cross', type=Path, default=Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')))
    args = parser.parse_args()
    print(json.dumps(audit(args.out, args.cross), indent=2))


if __name__ == '__main__':
    main()
