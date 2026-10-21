"""M3a: the built GF KMOD crosses the real M2 loader/facade boundary."""
import json
import os
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

import test_hwdev_module as module

ROOT = module.ROOT
OUT = ROOT / 'build/out'


def seal(image):
    image = bytearray(image)
    struct.pack_into('<I', image, 128, 0)
    struct.pack_into('<I', image, 128, zlib.crc32(image))
    return image


def verify_stubs(elf_data, entries):
    elf = module.mkmod.Elf(elf_data)
    payload, _, _ = elf.body(module.mkmod.POLICIES[11])
    base = elf.symbol('hwdev_import_slots')['value']
    for e in entries:
        offset = elf.symbol(e['name'])['value']
        assert payload[offset:offset+6] == b'\xff\x25' + struct.pack('<I', base + 4*e['index']), e
    assert 'kdebug_import_slots' not in elf.symbols
    assert 'recover_hal_wait_frame' in elf.symbols
    print('PASS 35 tail-jump destinations, address-only callback stub, no kdebug slots')


def main():
    image = (OUT/'hwdev.mod').read_bytes()
    elf_data = (OUT/'hwdev/hwdev.elf').read_bytes()
    meta = json.loads((ROOT/'hwdev/generated/hwdev_imports.json').read_text())
    contract = int(re.search(r'#define HWDEV_CONTRACT_CRC (0x\w+)U',
                   (ROOT/'hwdev/generated/hwdev_contract.h').read_text())[1], 16)
    assert meta['module_abi_gen'] == struct.unpack_from('<I', image, 24)[0] == contract
    cross = Path(os.environ['CROSS_DIR'])/'bin'
    assert not subprocess.check_output([str(cross/'i386-elf-nm'), '-u', str(OUT/'hwdev/hwdev.elf')])
    verify_stubs(elf_data, meta['imports'])
    cases = [('actual-GF', 0, 0, image), ('actual-GF-core-contract', 5, 6, image),
             ('actual-GF-core-layout', 6, 6, image), ('actual-GF-kernel-pair', 9, 4, image)]
    symbols = {line.split()[-1]: int(line.split()[0], 16) for line in
               subprocess.check_output([str(cross/'i386-elf-nm'), '--defined-only',
                                        str(OUT/'hwdev/hwdev.elf')], text=True).splitlines()}
    h = struct.unpack_from('<36I', image)
    for label, relative in [('core-image', 24), ('contract', 28), ('layout', 40)]:
        b = bytearray(image)
        pos = h[11] + symbols['manifest'] + relative
        struct.pack_into('<I', b, pos, struct.unpack_from('<I', b, pos)[0] ^ 1)
        cases.append(('actual-GF-' + label, 0, 10, seal(b)))
    b = bytearray(image)
    start = h[23]
    b[start:start+8], b[start+12:start+20] = b[start+12:start+20], b[start:start+8]
    swapped = seal(b)
    cases.append(('actual-GF-import-order', 0, 12, swapped))
    b = bytearray(image)
    struct.pack_into('<I', b, 24*4, h[24]-1)
    shortened = seal(b)
    cases.append(('actual-GF-import-count', 0, 6, shortened))
    with tempfile.TemporaryDirectory(prefix='hwdev-image-') as td:
        folder = Path(td)
        exe = folder/'host'
        command = module.build(exe)
        command[1:1] = ['-DHWDEV_REAL_IMAGE', '-I'+str(OUT/'hwdev')]
        subprocess.run(command, check=True)
        module.run(exe, cases)
        if '--mutate' in sys.argv:
            # Real image table corruption must turn the positive acceptance RED.
            for label, changed in [('order', swapped), ('count', shortened)]:
                try:
                    module.run(exe, [('mutant-'+label, 0, 0, changed)])
                except AssertionError as exc:
                    assert exc.args[0][1] == 1 and b'FAIL ' in exc.args[0][2], exc
                    print('MUTATE import-'+label+' RED')
                else:
                    raise AssertionError('survived import-'+label)
            source = (ROOT/'tools/gen_hwdev_contract.py').read_text()
            old = '4*e["index"]'
            assert source.count(old) == 1
            print('ANCHOR tools/gen_hwdev_contract.py stub displacement: 1')
            # Execute the actual generator with one wrong displacement, then link
            # the real GF objects against those stubs; the artifact oracle must fail.
            namespace = {'__file__': str(ROOT/'tools/gen_hwdev_contract.py'), '__name__': 'mutant'}
            exec(compile(source.replace(old, '4*(e["index"]+1)'), '<mutant>', 'exec'), namespace)
            namespace['generate'](str(cross/'i386-elf-gcc'), module.FLAGS, module.INCLUDES, folder)
            obj = folder/'hwdev_import_stubs.o'
            subprocess.run([str(cross/'i386-elf-gcc'), '-m32', '-c',
                            str(folder/'hwdev_import_stubs.S'), '-o', str(obj)], check=True)
            objects = [OUT/'hwdev'/ (n+'.o') for n in
                       ('gfinit_bus', 'gfinit', 'gfinit_plan', 'gfinit_resources', 'gfinit_decode',
                        'gfinit_exec', 'gfinit_pll', 'recover_gf', 'hwdev_main', 'hwdev_import_slots')]
            linked = folder/'mutant.elf'
            subprocess.run([str(cross/'i386-elf-ld'), '-m', 'elf_i386', '--emit-relocs',
                            '-T', str(ROOT/'build/hwdev.ld'), *map(str, objects), str(obj),
                            '-o', str(linked)], check=True)
            try:
                verify_stubs(linked.read_bytes(), meta['imports'])
            except AssertionError:
                print('MUTATE stub-displacement RED')
            else:
                raise AssertionError('survived stub displacement')
    print(f'PASS actual GF image={len(image)} contract={contract:08x} unresolved=0')


if __name__ == '__main__':
    main()
