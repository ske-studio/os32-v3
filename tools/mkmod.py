#!/usr/bin/env python3
"""Build an uncompressed KMOD v1 container from a VA-zero i386 --emit-relocs ELF.

The product build packages kdebug separately from the resident kernel. Metadata
comes from gen_kdebug_imports.py; exports come from the linked kdebug_ops
initializer (version followed by the sixteen function pointers).
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import zlib

HEADER_SIZE = 144
PAGE_SIZE = 4096
MAX_SIZE = 262144
MAX_RELOCS = 16384
MAX_IMPORTS = 1024
OPS_NAMES = ('selftest_run', 'selftest_post_exec', 'selftest_post_unicode',
             'selftest_post_probe', 'crt_diag_bootlog', 'gcap_capture',
             'gcap_active', 'gcap_keep_if', 'gcap_insn_abort', 'gcap_port_passed',
             'gcap_pass_in', 'gcap_pass_out', 'gcap_note_in', 'gcap_note_out',
             'gcap_detach', 'gcap_release')
SECTION_NAMES = ('.text', '.rodata', '.data', '.bss')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def align(value):
    return (value + PAGE_SIZE - 1) & -PAGE_SIZE


def span(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset,
            'ELF range outside file')
    return data[offset:offset + size]


def cstring(data, offset):
    require(0 <= offset < len(data), 'ELF string offset outside table')
    end = data.find(b'\0', offset)
    require(end >= 0, 'unterminated ELF string')
    return data[offset:end].decode('ascii')


class Elf:
    """Strict ELF32 section/symbol reader; no external parsing dependencies."""
    def __init__(self, data):
        self.data = data
        header = struct.unpack('<16sHHIIIIIHHHHHH', span(data, 0, 52))
        ident, kind, machine, version, self.entry, _, shoff, _, ehsize, _, _, shsize, count, strings = header
        require(ident[:7] == b'\x7fELF\x01\x01\x01' and kind == 2 and
                machine == 3 and version == 1 and ehsize == 52,
                'requires linked ELF32 little-endian i386 ET_EXEC')
        require(shsize == 40 and count > 0 and 0 < strings < count,
                'invalid ELF section table (extended indexes unsupported)')
        raw = span(data, shoff, count * shsize)
        self.sections = []
        for i in range(count):
            values = struct.unpack_from('<10I', raw, i * shsize)
            section = dict(zip(('name_off', 'type', 'flags', 'addr', 'offset',
                                'size', 'link', 'info', 'align', 'entsize'), values))
            if section['type'] != 8:
                span(data, section['offset'], section['size'])
            self.sections.append(section)
        require(self.sections[strings]['type'] == 3, 'invalid section name table')
        names = self.section_data(self.sections[strings])
        for section in self.sections:
            section['name'] = cstring(names, section['name_off'])
        self.symtabs = {}
        self.symbols = {}
        for index, section in enumerate(self.sections):
            if section['type'] != 2:
                continue
            require(section['entsize'] == 16 and section['size'] % 16 == 0 and
                    section['link'] < count, 'invalid ELF symbol table')
            strsec = self.sections[section['link']]
            require(strsec['type'] == 3, 'invalid symbol string table')
            strings_data = self.section_data(strsec)
            symbols = []
            for offset in range(0, section['size'], 16):
                name, value, size, info, other, shndx = struct.unpack_from(
                    '<IIIBBH', self.section_data(section), offset)
                symbol = dict(name=cstring(strings_data, name), value=value,
                              size=size, info=info, shndx=shndx)
                require(shndx == 0 or shndx < count or shndx == 0xfff1,
                        'unsupported ELF symbol section')
                require(not (shndx == 0 and (name or value or size or info)),
                        'unresolved ELF symbol: ' + symbol['name'])
                symbols.append(symbol)
                if symbol['name'] and info >> 4 in (1, 2):
                    require(symbol['name'] not in self.symbols,
                            'duplicate global ELF symbol')
                    self.symbols[symbol['name']] = symbol
            self.symtabs[index] = symbols
        require(bool(self.symtabs), 'missing ELF symbols; use --emit-relocs')

    def section_data(self, section):
        return span(self.data, section['offset'], section['size'])

    def symbol(self, name):
        require(name in self.symbols, 'missing ELF symbol: ' + name)
        return self.symbols[name]

    def body(self):
        alloc = [s for s in self.sections if s['flags'] & 2 and s['size']]
        require(all(s['name'] in SECTION_NAMES for s in alloc),
                'unexpected allocated ELF section')
        result = []
        for name in SECTION_NAMES:
            matches = [s for s in alloc if s['name'] == name]
            require(len(matches) <= 1, 'duplicate allocated ELF section')
            result.append(matches[0] if matches else None)
        text, ro, data, bss = result
        require(text is not None and text['addr'] == 0 and text['flags'] & 4,
                'module text must start at VA zero')
        for section in alloc:
            require(section['type'] == (8 if section['name'] == '.bss' else 1),
                    'incorrect allocated section type')
            require(section['addr'] <= MAX_SIZE and
                    section['size'] <= MAX_SIZE - section['addr'], 'body too large')
        ordered = sorted(alloc, key=lambda s: s['addr'])
        for left, right in zip(ordered, ordered[1:]):
            require(left['addr'] + left['size'] <= right['addr'],
                    'overlapping allocated ELF sections')
        payload_size = max(s['addr'] + s['size'] for s in alloc if s['type'] != 8)
        require(bss is None or bss['addr'] >= payload_size, 'bss overlaps payload')
        mem_size = align(max(s['addr'] + s['size'] for s in alloc))
        payload = bytearray(payload_size)
        for section in alloc:
            if section['type'] != 8:
                payload[section['addr']:section['addr'] + section['size']] = self.section_data(section)
        ranges = [(s['addr'], s['size']) if s else
                  (payload_size if name == '.bss' else 0, 0)
                  for name, s in zip(SECTION_NAMES, result)]
        return payload, mem_size, ranges

    def relocations(self, payload, mem_size):
        relocs = []
        for section in self.sections:
            if section['type'] not in (4, 9):
                continue
            require(section['info'] < len(self.sections), 'invalid relocation target')
            target = self.sections[section['info']]
            if not target['flags'] & 2:
                continue
            require(section['type'] == 9 and section['entsize'] == 8 and
                    section['size'] % 8 == 0 and section['link'] in self.symtabs,
                    'requires i386 REL relocation records')
            symbols = self.symtabs[section['link']]
            for off in range(0, section['size'], 8):
                where, info = struct.unpack_from('<II', self.section_data(section), off)
                sym_index, kind = info >> 8, info & 255
                require(sym_index < len(symbols), 'relocation symbol index out of range')
                symbol = symbols[sym_index]
                require(symbol['shndx'] != 0, 'unresolved relocation symbol')
                require(kind in (1, 2), 'unsupported i386 relocation type: ' + str(kind))
                require(where >= target['addr'] and where - target['addr'] <= target['size'] and
                        4 <= target['size'] - (where - target['addr']) and
                        where <= len(payload) and 4 <= len(payload) - where,
                        'relocation write outside payload section')
                # PC-relative references to absolute symbols would change when
                # moved; only references to body symbols cancel the load base.
                require(symbol['shndx'] < len(self.sections) and
                        self.sections[symbol['shndx']]['flags'] & 2,
                        'relocation must reference an allocated body symbol')
                if kind == 2:
                    continue
                value = struct.unpack_from('<I', payload, where)[0]
                require(value <= mem_size, 'relocation value outside body')
                relocs.append(where)
        relocs.sort()
        require(len(relocs) <= MAX_RELOCS, 'too many relocations')
        require(all(b >= a + 4 for a, b in zip(relocs, relocs[1:])),
                'overlapping relocation writes')
        return relocs


def pair_crc(image):
    require(len(image) >= 16, 'truncated VK32 header')
    magic, header_size, version, count = struct.unpack_from('<4I', image)
    require(magic == 0x32334b56 and version == 2 and 1 <= count <= 4 and
            header_size == 24 + 20 * count and header_size <= len(image),
            'invalid VK32 v2 header')
    size, crc = struct.unpack_from('<II', image, header_size - 8)
    require(size == len(image), 'VK32 image size mismatch')
    checked = bytearray(image)
    struct.pack_into('<I', checked, header_size - 4, 0)
    require(zlib.crc32(checked) == crc, 'VK32 Image CRC mismatch')
    return crc


def name_bytes(name):
    require(isinstance(name, str), 'name must be a string')
    data = name.encode('ascii')
    require(0 < len(data) <= 63 and b'\0' not in data, 'invalid module name')
    return data


def build(elf_data, metadata, vmkernel, kapi_abi_gen=75, layout_gen=3):
    elf = Elf(elf_data)
    payload, mem_size, ranges = elf.body()
    relocs = elf.relocations(payload, mem_size)
    text_off, text_size = ranges[0]
    data_off, data_size = ranges[2]
    init = elf.symbol('kdebug_init')['value']
    require(text_off <= init < text_off + text_size, 'init outside text')
    imports = metadata['imports']
    require(len(imports) <= MAX_IMPORTS, 'too many imports')
    import_table, export_table, names = bytearray(), bytearray(), bytearray()
    seen_names = set()
    if imports:
        slots = elf.symbol('kdebug_import_slots')
        require(slots['value'] == data_off and slots['size'] == len(imports) * 4 and
                slots['size'] <= data_size and data_off % 4 == 0,
                'import slots must be the exact array at data start')
    for index, record in enumerate(imports):
        name = name_bytes(record['name'])
        kind, ver = record['kind'], record['type_ver']
        require(record['index'] == index and 1 <= kind <= 5 and 1 <= ver <= 65535,
                'invalid import metadata')
        require(name not in seen_names, 'duplicate import name')
        seen_names.add(name)
        slot_off = data_off + 4 * index
        require(not any(r < slot_off + 4 and slot_off < r + 4 for r in relocs),
                'relocation overlaps import slot')
        require(struct.unpack_from('<I', payload, slot_off)[0] == 0,
                'import slot must be initially zero')
        import_table += struct.pack('<IHHI', fnv1a(name), ver, kind, slot_off)
        names += name + b'\0'
    ops_version = 1
    if 'kdebug_ops' in elf.symbols:
        ops = elf.symbol('kdebug_ops')
        require(ops['size'] == 4 * (len(OPS_NAMES) + 1), 'invalid kdebug_ops size')
        require(any(off <= ops['value'] and ops['value'] - off <= size and
                    ops['size'] <= size - (ops['value'] - off) for off, size in ranges[:3]),
                'kdebug_ops outside payload sections')
        values = struct.unpack_from('<17I', payload, ops['value'])
        require(values[0] == ops_version, 'unsupported kdebug_ops version')
        for slot, (name, entry) in enumerate(zip(OPS_NAMES, values[1:]), 1):
            pointer_off = ops['value'] + slot * 4
            # A function at text VA zero is non-null in the relocated image.
            if pointer_off not in relocs:
                require(entry == 0, 'export pointer lacks R_386_32 relocation')
                continue
            require(text_off <= entry < text_off + text_size, 'export outside text')
            encoded = name_bytes(name)
            export_table += struct.pack('<IHHI', fnv1a(encoded), slot, 1, entry)
            names += encoded + b'\0'
    reloc_table = b''.join(struct.pack('<I', r) for r in relocs)
    tables = (reloc_table, import_table, export_table, names)
    offsets = []
    offset = HEADER_SIZE
    for table in tables:
        offsets.append(offset)
        offset += len(table)
    prefix_size = offset
    payload_offset = align(prefix_size)
    image_size = payload_offset + len(payload)
    require(image_size <= MAX_SIZE and mem_size <= MAX_SIZE, 'module exceeds size limit')
    abi = metadata['module_abi_gen']
    require(all(isinstance(v, int) and 0 <= v <= 0xffffffff
                for v in (abi, kapi_abi_gen, layout_gen)), 'invalid ABI generation')
    pair = pair_crc(vmkernel)
    header = [int.from_bytes(b'KMOD', 'little'), 1, HEADER_SIZE, image_size,
              kapi_abi_gen, layout_gen, abi, 1, 2, 1, mem_size,
              payload_offset, len(payload), *[n for r in ranges for n in r],
              offsets[0], len(relocs), offsets[1], len(imports),
              offsets[2], len(export_table) // 12, offsets[3], len(names),
              init, 0, pair, 0, ops_version, 0, 0]
    image = bytearray(struct.pack('<36I', *header) + b''.join(tables))
    image += bytes(payload_offset - len(image)) + payload
    struct.pack_into('<I', image, 128, zlib.crc32(image))
    stats = dict(prefix_size=prefix_size, prefix_alloc=payload_offset,
                 reloc_count=len(relocs), import_count=len(imports),
                 export_count=len(export_table) // 12, names_size=len(names),
                 pair_image_crc=pair, payload_size=len(payload), mem_size=mem_size)
    return bytes(image), stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--vmkernel', type=Path, required=True)
    parser.add_argument('--kapi-abi-gen', type=lambda s: int(s, 0), default=75)
    parser.add_argument('--layout-gen', type=lambda s: int(s, 0), default=3)
    parser.add_argument('-o', '--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        image, stats = build(args.elf.read_bytes(), json.loads(args.metadata.read_text()),
                             args.vmkernel.read_bytes(), args.kapi_abi_gen, args.layout_gen)
        temporary = args.output.with_name(args.output.name + '.tmp')
        temporary.write_bytes(image)
        temporary.replace(args.output)
    except (OSError, ValueError, KeyError, TypeError, struct.error) as exc:
        print('mkmod: ' + str(exc), file=sys.stderr)
        return 1
    print('KMOD prefix={prefix_size} B allocated={prefix_alloc} B '
          '(header=144 reloc={reloc_count} imports={import_count} '
          'exports={export_count} names={names_size} B) '
          'payload={payload_size} B mem={mem_size} B pair={pair_image_crc:08x}'.format(**stats))
    return 0


if __name__ == '__main__':
    sys.exit(main())
