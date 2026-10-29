"""L02 resident match wiring, fixed PCI layout and contract regeneration.

--prepare compiles copied substitutions only; it does not execute mutants.
"""
import argparse
from hwdev_contract_fixture import assert_hwdev_contract
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OLD = 'E82557_VENDOR, E82557_DEVICE,'
NEW = 'E82557_VENDOR, E82557_DEVICE ^ 1,'
COPIES = ['include/pci_dev_contract.h', 'include/pci_bind_contract.h',
          'include/types.h', 'drivers/pci_decode.h', 'drivers/pci.h',
          'drivers/pci_bind.h', 'drivers/pci_bind_match.c', 'kernel/lan_module.h',
          'platform/pc98/lan_match.c', 'tools/tests/dep_dir_a_l02_host.c']


def build(folder, body):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'platform/pc98/lan_match.c').write_text(body)
    exe = folder / 'match'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    *['-I' + str(folder / p) for p in ('include', 'drivers', 'kernel')],
                    *['-I' + str(ROOT / p) for p in
                      ('include', 'drivers', 'kernel', 'fs', 'sdk/include/os32')],
                    str(folder / 'tools/tests/dep_dir_a_l02_host.c'),
                    str(folder / 'platform/pc98/lan_match.c'),
                    str(folder / 'drivers/pci_bind_match.c'), '-o', str(exe)], check=True)
    return exe


def contracts():
    for header in ('include/pci_dev_contract.h', 'include/pci_bind_contract.h'):
        result = subprocess.run(['make', '-n', '-j1', '-W', header,
                                 'build/out/lan/lan_imports.h'], cwd=ROOT,
                                capture_output=True, text=True, check=True)
        assert 'gen_sqlite_imports.py --module lan ' in result.stdout, result.stdout
    assert hashlib.sha256((ROOT / 'tools/hwdev_layout.json').read_bytes()).hexdigest() == HWDEV_SHA
    assert_hwdev_contract(ROOT)
    print('PASS B1/B2: fixed hwdev inventory/CRCs, both LAN contract dependencies')


def generation(folder):
    import sys
    sys.path.insert(0, str(ROOT / 'tools'))
    import mkmod
    from test_module_image import build_validator, validate, fnv, crc, word
    fixture = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l02.json').read_text())
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    metadata = json.loads((ROOT / 'build/out/lan/lan_imports.json').read_text())
    current = metadata['module_abi_gen']
    old = fixture['lan_abi_gen']
    assert old != current
    exe = build_validator(folder)
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=current, pair=mkmod.pair_crc((ROOT / 'build/out/vmkernel.lz4').read_bytes()),
                    ops=1, module_id=9, runner='qemu', exports=[(1, fnv('initialize'), 1)],
                    imports=[(fnv(e['name']), e['type_ver'], e['kind']) for e in metadata['imports']])
    image = (ROOT / 'build/out/lan.mod').read_bytes()
    result = validate(exe, image, **contract)
    assert result.returncode == 0 and result.stdout == b'GREEN\n', result
    stale = bytearray(image)
    word(stale, 6, old)
    result = validate(exe, crc(stale), **contract)
    assert result.returncode == 1 and result.stdout == b'RED 06\n', result
    print(f'PASS paired LAN accepted, L01 ABI rejected: 0x{old:08x} -> 0x{current:08x}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    body = (ROOT / 'platform/pc98/lan_match.c').read_text()
    assert body.count(OLD) == 1
    with tempfile.TemporaryDirectory(prefix='dep-dir-a-l02-') as directory:
        folder = Path(directory)
        exe = build(folder / 'control', body)
        subprocess.run(['qemu-i386', str(exe)], check=True)
        contracts()
        generation(folder)
        print('PASS ILP32 PCI ABI; resident table/callback; ID/class matches and mismatches')
        if args.prepare:
            build(folder / 'prepared', body.replace(OLD, NEW))
            import test_e82557_drv as driver
            for index, (rel, old, new, label) in enumerate(driver.MUTATIONS):
                dest = folder / ('driver-' + str(index))
                dest.mkdir()
                driver.host_build(dest, (rel, driver.mutation_text(rel, old, new, label)))
            print('PASS preparation: copied new headers, unique anchors, substitutions compile -Werror')
        if args.mutate:
            exe = build(folder / 'mutant', body.replace(OLD, NEW))
            result = subprocess.run(['qemu-i386', str(exe)])
            assert result.returncode == 1
            print('RED runtime: machine device ID changed (compiled successfully)')


HWDEV_SHA = '64930e8502216bef464a5c17260bcf4ebb035802587b1723341ca9e7c05a751a'
if __name__ == '__main__':
    main()
