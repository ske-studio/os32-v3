#!/usr/bin/env python3
"""Exercise LAN acceptance with synthetic evidence and product mutations."""
import argparse
from pathlib import Path
import struct
import sys
import tempfile
import types
from unittest.mock import patch
import kboot_run as meter

CHECK = dict(name='ppc-lan-p29', tier=1, budget=60, needs=[],
             covers=['tools/ppc/test_lan_p29.py'],
             inputs=['tools/ppc/test_lan_p29.py', 'tools/ppc/kboot_run.py',
                     'tools/ppc_lan_host.py', 'tools/ppc/run.py', 'tools/ppc/kboot_mutations.py'])


def cases(mod, elf):
    boot = bytes(range(16))
    data = bytes(range(256))
    serial = (b'KBOOT OF-CAPTURE ram=4096 admitted=2048 tb_hz=100\r\n'
              b'KBOOT MMU-D root=1 hpt=0x1000 bytes=512 ram_bat=0\r\n'
              b'LAN STATE state=5 link=1\r\n'
              b'KBOOT ALIVE tick_delta=2000 tb_us=2000000 of_calls_delta=0\r\n')
    lines = ['beacon seq=0 drop_records=0 drop_bytes=0',
             'response 1 OK u32=' + ','.join(['5'] + ['0']*16),
             'response 2 OK tick=1 tb_hz=100 tb=2 us=3',
             'response 3 OK base=0x0 size=0x800 base=0x100 size=0x100 base=0x1000 size=0x200 base=0x2000 size=0x300',
             'response 4 OK ' + (boot + bytes.fromhex('525400123456ffffffffffff')).hex(),
             'response 5 OK ' + data.hex()]
    mapping = ' .bss.pool 0x2000 0x300 platform/powermac/gmac_dma.o\n'
    def judge(s=serial, c=serial, h=lines, m=mapping):
        return mod.lan_judge(s, c, h, elf, m, boot)
    assert judge() is None, judge()
    altered = bytearray(serial); altered[2] ^= 1
    badpeek = lines.copy(); badpeek[-1] = badpeek[-1][:-2] + '00'
    badpeer = lines.copy(); badpeer[4] = badpeer[4][:-12] + '020000000001'
    assert judge(h=badpeer) == 'LAN diagnostic identity'
    tests = [('capture', dict(c=bytes(altered))), ('gap', dict(h=lines+['gap: 1'])),
             ('drop', dict(h=lines+['log seq=1 drop_records=1 drop_bytes=0'])),
             ('peek', dict(h=badpeek)), ('memory', dict(m=mapping.replace('0x300', '0x301'))),
             ('state', dict(s=serial.replace(b'state=5', b'state=4')))]
    return [(name, judge(**args)) for name, args in tests]


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true'); args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        elf = Path(tmp)/'fixture.elf'
        header = bytearray(0x100)
        header[:6] = b'\x7fELF\x01\x02'
        struct.pack_into('>II', header, 24, 0x1000, 52)
        struct.pack_into('>HH', header, 42, 32, 1)
        struct.pack_into('>IIIII', header, 52, 1, 0x100, 0x1000, 0, 256)
        elf.write_bytes(header + bytes(range(256)))
        for name, reason in cases(meter, elf):
            assert reason, 'H:A12 false-accept ' + name
        if args.mutate:
            source = Path(meter.__file__).read_text()
            mutations = [
                ('capture', 'if capture_bytes[:len(expected)] != expected:', 'if False:'),
                ('peek', 'if bytes.fromhex(responses[5]) != entry_bytes(elf_path)[1]:', 'if False:'),
                ('gap', "if any(re.search(r'gap|duplicate|out of order|rollback', line) for line in host_lines):", 'if False:'),
            ]
            for name, before, after in mutations:
                assert source.count(before) == 1
                mutant = types.ModuleType('mutant')
                exec(compile(source.replace(before, after), meter.__file__, 'exec'), mutant.__dict__)
                result = dict(cases(mutant, elf))
                assert result[name] is None, (name, result[name])
                print('RED H:A12 false-accept ' + name)
            before = '        return 3\n    for cpu in (['
            assert source.count(before) == 1
            mutant = types.ModuleType('mutant')
            exec(compile(source.replace(before, '        return 0\n    for cpu in (['), meter.__file__, 'exec'), mutant.__dict__)
            (Path(tmp)/'lan-skip').touch()
            with patch.object(sys, 'argv', ['meter', '--elf', str(elf), '--cpu', '7450']), patch.object(mutant, 'accept', return_value=Path(tmp)):
                assert mutant.main() == 0
            print('RED H:A12 skip-as-pass')
    print('PASS H29')


if __name__ == '__main__':
    main()
