"""Translate final PPC32 ELF RELA to KMOD fixups (LE table, BE body)."""
import struct


def require(ok, message):
    if not ok:
        raise ValueError(message)


def instruction(where, text_size):
    require(where % 4 == 0 and 0 <= where <= text_size - 4,
            'unaligned or incomplete PPC text instruction')


def ppc_relocations(elf, payload: bytes, mem_size: int, policy) -> bytes:
    header = elf.data[:52]
    require(len(header) == 52 and header[:7] == b'\x7fELF\x01\x02\x01'
            and struct.unpack_from('>HHI', header, 16) == (2, 20, 1),
            'requires ELF32 BE PowerPC ET_EXEC')
    phoff = struct.unpack_from('>I', header, 28)[0]
    phsize, phnum = struct.unpack_from('>HH', header, 42)
    require(not phnum or (phsize == 32 and phoff + phsize * phnum <= len(elf.data)),
            'invalid program headers')
    for index in range(phnum):
        require(struct.unpack_from('>I', elf.data, phoff + index * phsize)[0]
                not in (2, 3), 'dynamic PPC module')
    text, = [s for s in elf.sections if s['name'] == '.text' and s['flags'] & 2]
    require(text['addr'] == 0, 'text must start at zero')
    instruction(elf.entry, text['size'])
    instruction(elf.symbol(policy['init_symbol'])['value'], text['size'])
    writes = []
    records = []
    for sec in elf.sections:
        require(sec['type'] not in (6, 11), 'dynamic ELF section')
        if sec['flags'] & 2:
            require(sec['name'] in ('.text', '.rodata', '.data', '.bss'),
                    'unexpected allocated ELF section')
        if sec['type'] not in (4, 9):
            continue
        require(sec['info'] < len(elf.sections), 'invalid relocation section')
        dest = elf.sections[sec['info']]
        if not dest['flags'] & 2:
            continue
        require(dest['type'] == 1 and sec['type'] == 4 and sec['entsize'] == 12
                and sec['size'] % 12 == 0 and sec['link'] in elf.symtabs,
                'invalid PPC RELA table')
        symbols = elf.symtabs[sec['link']]
        for place, info, addend in struct.iter_unpack('>IIi', elf.section_data(sec)):
            kind = info & 255
            require(info >> 8 < len(symbols), 'invalid relocation symbol index')
            symbol = symbols[info >> 8]
            idx = symbol['shndx']
            require(0 < idx < len(elf.sections) and elf.sections[idx]['flags'] & 2,
                    'relocation symbol outside allocated body')
            owner = elf.sections[idx]
            require(owner['addr'] <= symbol['value'] <= owner['addr'] + owner['size'],
                    'relocation symbol outside defining section')
            goal = symbol['value'] + addend
            require(0 <= goal < mem_size or
                    (goal == mem_size and symbol['name'] == '__module_end'),
                    'relocation target outside body')
            require(kind in (1, 4, 5, 6, 10, 11, 26), 'unsupported PPC relocation')
            width = 2 if kind in (4, 5, 6) else 4
            require(dest['addr'] <= place and place + width <= min(
                dest['addr'] + dest['size'], len(payload)), 'write outside payload')
            writes.append((place, place + width))
            word = int.from_bytes(payload[place:place + width], 'big')
            delta = goal - place
            if kind in (10, 11):
                require(dest is text, 'branch outside text')
                instruction(place, text['size'])
                instruction(goal, text['size'])
                bits, opcode = (26, 18) if kind == 10 else (16, 16)
                limit = 1 << (bits - 1)
                mask = 2 * limit - 4
                require(-limit <= delta < limit, 'branch displacement out of range')
                require(word >> 26 == opcode and not word & 2 and
                        word & mask == delta & mask, 'invalid linked branch')
            elif kind == 26:
                require(-(1 << 31) <= delta < 1 << 31 and word == delta & 0xffffffff,
                        'invalid linked REL32')
            else:
                require(place % width == 0, 'unaligned absolute relocation')
                records.append((place, kind, goal))
    writes.sort()
    require(all(end <= right for (_, end), (right, _) in zip(writes, writes[1:])),
            'overlapping PPC relocations')
    require(len(records) <= policy['reloc_max'], 'too many PPC relocations')
    ops = elf.symbols.get(policy['ops_symbol'])
    if ops:
        pointers = {p: goal for p, kind, goal in records if kind == 1}
        for slot in range(policy['ops_slots']):
            p = ops['value'] + 4 * (1 + slot)
            require(p + 4 <= len(payload), 'export outside payload')
            value = int.from_bytes(payload[p:p + 4], 'big')
            if value or p in pointers:
                require(pointers.get(p) == value, 'invalid export pointer')
                instruction(value, text['size'])
    return b''.join(struct.pack('<III', *record) for record in sorted(records))
