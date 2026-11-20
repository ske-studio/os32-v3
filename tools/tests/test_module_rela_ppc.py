"""PowerPC module alignment design, pack/loader boundary and L1 fixtures.
The KMOD pack/loader contract in docs/design/kernel/module/module_image.md
requires relocation evidence, complete export pointers, and a page-rounded
module end marker (as in build/fep-module.ld).
Expectations use the ELF PPC ABI BD/LI fields and independent KMOD wire bytes.
"""
import host32
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
PACK = ROOT / 'tools/ppc/module_rela.py'
FIXUP = ROOT / 'kernel/module/module_fixup_ppc.inc'
POLICY = dict(init_symbol='entry', ops_symbol='ops', ops_slots=1, reloc_max=8)


class WireElf:
    """Read the fixture into the same section/symbol interface as mkmod.Elf."""
    def __init__(self, data):
        self.data = data
        self.entry, _, shoff = struct.unpack_from('>3I', data, 24)
        count, names = struct.unpack_from('>HH', data, 48)
        keys = ('name_offset', 'type', 'flags', 'addr', 'offset', 'size',
                'link', 'info', 'align', 'entsize')
        self.sections = [dict(zip(keys, struct.unpack_from('>10I', data, shoff + 40*i)))
                         for i in range(count)]
        strings = self.section_data(self.sections[names])
        self.symbols = {}
        self.symtabs = {}
        for index, sec in enumerate(self.sections):
            sec['name'] = strings[sec['name_offset']:].split(b'\0', 1)[0].decode()
            if sec['type'] == 2:
                table = self.section_data(self.sections[sec['link']])
                symbols = []
                for name, value, size, info, _, shndx in struct.iter_unpack(
                        '>IIIBBH', self.section_data(sec)):
                    name = table[name:].split(b'\0', 1)[0].decode()
                    symbol = dict(name=name, value=value, size=size, info=info, shndx=shndx)
                    symbols.append(symbol)
                    if name:
                        self.symbols[name] = symbol
                self.symtabs[index] = symbols

    def section_data(self, sec):
        return self.data[sec['offset']:sec['offset'] + sec['size']]

    def symbol(self, name):
        return self.symbols[name]


def fixture(kind=1, place=0, symbol=0x104, addend=-4, word=0x100, entry=0, ops=False,
            retained=True, target="target"):
    body = bytearray(0x10000)
    struct.pack_into('>I', body, place, word)
    strings = b'\0.text\0.rela.text\0.symtab\0.strtab\0target\0entry\0ops\0.bss\0__module_end\0'
    symbols = bytearray(16)
    for name, value in [(target, symbol), ('entry', 0)] + ([('ops', 4)] if ops else []):
        symbols.extend(struct.pack('>IIIBBH', strings.index(name.encode()), value,
                                   0, 0x10, 0, 5 if name == "__module_end" else 1))
    rela = struct.pack('>IIi', place, 256 + kind, addend)
    data = bytearray(52)
    sections = bytearray(40)
    for name, typ, flags, content, link, info, align, stride in (
            ('.text', 1, 6, body, 0, 0, 4, 0), ('.rela.text', 4 if retained else 0, 0, rela if retained else b'', 3, 1, 4, 12),
            ('.symtab', 2, 0, symbols, 4, 1, 4, 16), ('.strtab', 3, 0, strings, 0, 0, 1, 0)):
        sections.extend(struct.pack('>10I', strings.index(name.encode()), typ, flags,
                                    0, len(data), len(content), link, info, align, stride))
        data.extend(content)
    sections.extend(struct.pack('>10I', strings.index(b'.bss'), 8, 3, len(body),
                                len(data), 4, 0, 0, 4, 0))
    data.extend(bytes(-len(data) % 4))
    struct.pack_into('>16sHHIIIIIHHHHHH', data, 0, b'\x7fELF\x01\x02\x01',
                     2, 20, 1, entry, 0, len(data), 0, 52, 0, 0, 40, 6, 4)
    return WireElf(bytes(data + sections)), body, len(body) + 4096, POLICY


def rejected(pack, args):
    try:
        pack(*args)
    except ValueError:
        return True
    return False


def negative_addend(pack):
    return pack(*fixture()) == bytes.fromhex('00000000 01000000 00010000')


def branch_range(pack):
    for delta in (-32772, -32768, 32764, 32768):
        place = 32772 if delta < 0 else 0
        args = fixture(11, place, place + delta, 0, 0x40000000 | (delta & 0xfffc))
        if rejected(pack, args) != (delta < -32768 or delta > 32764):
            return False
    return True


def alignment(pack):
    for kind, opcode in ((10, 0x48000000), (11, 0x40000000)):
        if not rejected(pack, fixture(kind, 2, 6, 0, opcode | 4)):
            return False
    if not rejected(pack, fixture(entry=2)):
        return False
    args = fixture(1, 8, 2, 0, 2, ops=True)
    return rejected(pack, args)


def relocation_evidence(pack):
    return rejected(pack, fixture(retained=False))


def export_fixups(pack):
    for place in (8, 10):
        for kind in (4, 5, 6):
            if not rejected(pack, fixture(kind, place, word=0, ops=True)):
                return False
    return pack(*fixture(1, 8, 0, 0, 0, ops=True)) == struct.pack('<III', 8, 1, 0)


def end_marker(pack):
    end = 0x11000
    args = fixture(symbol=end, addend=0, word=end, target='__module_end')
    try:
        record = pack(*args)
    except ValueError:
        return False
    return (record == struct.pack('<III', 0, 1, end)
            and rejected(pack, fixture(symbol=end, addend=0))
            and rejected(pack, fixture(symbol=end - 4, addend=0, target='__module_end')))


def changed(code, old, new):
    assert code.count(old) == 1, old
    return code.replace(old, new)


def packer(source):
    namespace = {}
    exec(compile(source, str(PACK), 'exec'), namespace)
    return namespace['ppc_relocations']


def run_host(directory, source):
    (directory / FIXUP.name).write_text(source)
    exe = directory / 'host'
    subprocess.run(['gcc', '-m32', '-nostdlib', '-nostdinc', '-static', '-fno-pie',
                    '-fno-stack-protector', '-I', directory, '-Iinclude', '-Ikernel/module',
                    'tools/tests/module_fixup_ppc_host.c', '-o', exe], check=True)
    return host32.run([str(exe)]).returncode


if __name__ == '__main__':
    source = PACK.read_text()
    mutations = [(negative_addend, "symbol['value'] + addend", "symbol['value']"),
                 (branch_range, '-limit <= delta < limit', 'True'),
                 (alignment, 'where % 4 == 0 and ', ''),
                 (relocation_evidence, 'require(writes,', 'require(True,'),
                 (export_fixups, 'require(kind == 1 and (place', 'require(True or (place'),
                 (end_marker, 'require(module_end or owner', 'require(owner')]
    for test, old, new in mutations:
        assert test(packer(source)), test.__name__
        if '--mutate' in sys.argv:
            assert not test(packer(changed(source, old, new))), test.__name__
            print('RED', test.__name__)
    with tempfile.TemporaryDirectory() as tmp:
        source = FIXUP.read_text()
        assert run_host(Path(tmp), source) == 0
        if '--mutate' in sys.argv:
            assert run_host(Path(tmp), changed(source, ' + 0x8000', '')) == 2
            print('RED HA rounding')
    print('PASS PPC module wire fixtures')
