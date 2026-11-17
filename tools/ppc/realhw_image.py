#!/usr/bin/env python3
"""Inspect a PowerPC ELF before offering it to real Open Firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys


def inspect(data, load_base, avail_end):
    errors = []
    info = dict(size=len(data), sha256=hashlib.sha256(data).hexdigest(), loads=[], notes=[], root_size=None)
    if load_base is None or avail_end is None:
        errors.append('load-base / available end 未確定')
    elif not (0 <= load_base < avail_end <= 0x100000000):
        errors.append('invalid available range')
    elif len(data) > avail_end - load_base:
        errors.append('ELF filesz exceeds available range')
    try:
        if len(data) < 52 or data[:7] != b'\x7fELF\x01\x02\x01':
            raise ValueError('not ELF32 MSB')
        h = struct.unpack_from('>16sHHIIIIIHHHHHH', data)
        _, typ, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = h
        info.update(entry=entry, flags=flags, type=typ, machine=machine)
        if typ != 2 or machine != 20 or version != 1 or ehsize != 52 or phsize != 32:
            raise ValueError('invalid PPC executable header')
        if phoff + phnum * phsize > len(data):
            raise ValueError('truncated program headers')
        for i in range(phnum):
            kind, off, va, pa, filesz, memsz, pflags, align = struct.unpack_from('>IIIIIIII', data, phoff+i*phsize)
            if kind not in (1, 4):
                continue
            if off + filesz > len(data):
                raise ValueError('truncated segment')
            if kind == 1:
                info['loads'].append(dict(offset=off, va=va, pa=pa, filesz=filesz, memsz=memsz, flags=pflags, align=align))
                if filesz > memsz or pa + memsz > 0x100000000 or va + memsz > 0x100000000:
                    errors.append('invalid PT_LOAD size')
                if align > 1 and (align & (align-1) or (va-off) % align):
                    errors.append('invalid PT_LOAD alignment')
                if load_base is not None and pa + memsz >= load_base:
                    errors.append('PT_LOAD end reaches load-base')
            else:
                end = off + filesz
                while off < end:
                    if off + 12 > end:
                        raise ValueError('truncated NOTE header')
                    namesz, descsz, ntype = struct.unpack_from('>III', data, off)
                    off += 12
                    name_end = off + namesz
                    desc = (name_end + 3) & ~3
                    next_note = (desc + descsz + 3) & ~3
                    if next_note > end:
                        raise ValueError('truncated NOTE')
                    info['notes'].append(dict(name=data[off:name_end].rstrip(b'\0').decode('ascii', 'backslashreplace'), type=ntype, desc_hex=data[desc:desc+descsz].hex()))
                    off = next_note
        if not info['loads'] or not any(x['flags'] & 1 and x['va'] <= entry < x['va'] + x['memsz'] for x in info['loads']):
            errors.append('entry outside executable PT_LOAD')
        if not info['notes']:
            errors.append('NOTE absent')
        if shnum:
            if shsize != 40 or shoff + shnum * shsize > len(data):
                raise ValueError('invalid section headers')
            sections = [struct.unpack_from('>IIIIIIIIII', data, shoff+i*shsize) for i in range(shnum)]
            symbols = {}
            for s in sections:
                if s[1] != 2:
                    continue
                off, size, link, entsize = s[4], s[5], s[6], s[9]
                if link >= shnum or entsize != 16 or off+size > len(data):
                    raise ValueError('invalid symbol table')
                strings = sections[link]
                if strings[4]+strings[5] > len(data):
                    raise ValueError('invalid string table')
                names = data[strings[4]:strings[4]+strings[5]]
                for pos in range(off, off+size, entsize):
                    name, value, _, _, _, _ = struct.unpack_from('>IIIBBH', data, pos)
                    if name < len(names):
                        end = names.find(b'\0', name)
                        if end >= 0:
                            symbols[names[name:end]] = value
            if b'__root_image_start' in symbols and b'__root_image_end' in symbols:
                start, end = symbols[b'__root_image_start'], symbols[b'__root_image_end']
                if end < start or not any(x['va'] <= start <= end <= x['va']+x['filesz'] for x in info['loads']):
                    errors.append('invalid embedded root range')
                else:
                    info['root_size'] = end-start
    except (ValueError, struct.error) as exc:
        errors.append(str(exc))
    info['errors'] = errors
    return info


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('image', type=Path)
    p.add_argument('--load-base', type=lambda x: int(x, 0))
    p.add_argument('--avail-end', type=lambda x: int(x, 0))
    args = p.parse_args()
    try:
        result = inspect(args.image.read_bytes(), args.load_base, args.avail_end)
    except OSError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return int(bool(result['errors']))


if __name__ == '__main__':
    sys.exit(main())
