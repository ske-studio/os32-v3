"""Built GF KMOD: loader/facade, relocated calls and existing trace oracles."""
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
import test_hwdev_trace as trace
import test_hwdev_dispatch as dispatch
from gen_kdebug_imports import hwdev_gf_objects

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
    core = module.mkmod.Elf((OUT/'kdebug/kdebug.elf').read_bytes())
    gf_objects = hwdev_gf_objects()
    for name in gf_objects:
        names = subprocess.check_output([str(cross/'i386-elf-nm'), '-g', '--defined-only',
                                         str(OUT/'hwdev'/(name+'.o'))], text=True)
        for line in names.splitlines():
            symbol = line.split()[-1]
            assert symbol not in core.symbols, symbol
    for name in ('recover_gf_profile', 'recover_gf_screen_safe',
                 'recover_gf_pre', 'recover_gf_result'):
        assert name in core.symbols, name
    assert len((OUT/'kdebug.bin').read_bytes()) <= 192*1024
    for name, module_id in (('kdebug.bin', 1), ('hwdev.mod', 11)):
        data = (OUT/name).read_bytes()
        header = dict(zip(module.mkmod.HEADER_FIELDS, struct.unpack_from('<36I', data)))
        policy = module.mkmod.POLICIES[module_id]
        assert len(data) <= policy['image_max'], name
        assert header['MEM_SIZE'] <= policy['mem_max'], name
        assert header['PAYLOAD_OFF'] + header['MEM_SIZE'] <= policy['load_span_max'], name
        print(f'PASS {name}: image={len(data)} body={header["MEM_SIZE"]} prefix={header["PAYLOAD_OFF"]}')
    print('PASS GF body absent from core; S0 and shared GF log retained')
    verify_stubs(elf_data, meta['imports'])
    cases = [('actual-GF', 0, 0, image), ('actual-GF-core-contract', 5, 6, image),
             ('actual-GF-core-layout', 6, 6, image), ('actual-GF-kernel-pair', 9, 4, image)]
    # The decoder's local control() has the same name as the owner's BSS.
    symbols = {line.split()[-1]: int(line.split()[0], 16) for line in
               subprocess.check_output([str(cross/'i386-elf-nm'), '--defined-only',
                                        str(OUT/'hwdev/hwdev.elf')], text=True).splitlines()
               if line.split()[-1] != 'control' or line.split()[-2] == 'b'}
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
        # Seed a PAUSED bundle at the real private owner's layout. Only the
        # platform/trial callbacks are doubles; call/release execute KMOD code.
        owner_source = (ROOT/'kdebug/gfinit.c').read_text()
        owner_type = re.search(r'struct gfinit_owner \{.*?\n\};', owner_source, re.S)[0]
        header = '#include "kdebug/gfinit_bus.h"\n#include "kdebug/gfinit_plan.h"\n#include "hwdev/owner.h"\n' + owner_type + '\n'
        for name in ('control', 'owner', 'gf_lease'):
            header += '#define IMAGE_' + name.upper() + ' ' + str(symbols[name]) + 'U\n'
        (folder/'hwdev_owner_fixture.h').write_text(header)
        exe = folder/'host'
        command = module.build(exe)
        command[1:1] = ['-DHWDEV_REAL_IMAGE', '-I'+str(OUT/'hwdev'), '-I'+str(folder)]
        subprocess.run(command, check=True)
        module.run(exe, cases)
        print("PASS actual ops->call op5-10 and release_work -> relocated core stubs")
        trace.run(folder)
        trace.run(folder, mode_c=True)
        dispatch.main()
        if '--mutate' in sys.argv:
            # Break only the loaded body. Host white-box copies remain intact:
            # losing the trace macro substitution would let this mutant survive.
            b = bytearray(image)
            entry = symbols['gfinit_exec_run']
            pos = h[11] + entry
            trap = b'\xb8\xff\xff\xff\xff\xc3'  # return -1
            relocs = struct.unpack_from('<' + 'I'*h[22], image, h[21])
            assert all(r + 4 <= entry or r >= entry + len(trap) for r in relocs)
            b[pos:pos+len(trap)] = trap
            try:
                trace.run(folder, seal(b))
            except AssertionError as exc:
                assert exc.args[0][0] == 1 and b'FAIL ' in exc.args[0][1], exc
                print('MUTATE trace-image-exec-body RED')
            else:
                raise AssertionError('survived trace image body; substitution broken')
            # Keep the KMOD valid but redirect the executed recover_owner stub
            # to trial_end. A byte-only oracle is deliberately not used here.
            b = bytearray(image)
            pos = h[11] + symbols['recover_owner'] + 2
            old_slot = struct.unpack_from('<I', b, pos)[0]
            assert old_slot == symbols['hwdev_import_slots'] + 4*3
            struct.pack_into('<I', b, pos, old_slot - 4)
            try:
                module.run(exe, [('executed-stub-wrong-slot', 0, 0, seal(b))])
            except AssertionError:
                print('MUTATE executed-stub-wrong-slot RED')
            else:
                raise AssertionError('survived executed stub wrong slot')
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
                       (*gf_objects, 'hwdev_main', 'hwdev_import_slots')]
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
