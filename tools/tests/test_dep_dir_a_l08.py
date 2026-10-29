"""L08 PCI diagnostic KAPI boundary; --prepare builds but never runs a mutation."""
import argparse
from hwdev_contract_fixture import assert_hwdev_contract
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_shm_user import function
from test_dep_dir_a_l05a import digest, public_spec

ROOT = Path(__file__).resolve().parents[2]
COPIES = (
    'include/types.h', 'include/platform_device.h', 'include/pci_dev_contract.h',
    'include/pci_bind_contract.h', 'drivers/pci.h', 'drivers/pci_decode.h',
    'drivers/pci_bind.h', 'sdk/include/os32/os32_kapi_shared.h',
    'include/memmap.h', 'include/time_units.h',
    'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_generations.h',
    'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'tools/tests/dep_dir_a_l08_host.c', 'platform/pc98/device_pci.c',
)
OLD = 'return pci_cfg_read32(bus, dev, fn, reg);'
NEW = 'return pci_cfg_read32(bus, dev, fn, reg ^ 4u);'



def contracts():
    fixture = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l08.json').read_text())
    abi = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l05a.json').read_text())
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    assert digest(json.dumps(public_spec(spec), sort_keys=True).encode()) == abi['public_sha256']
    for path, expected in abi['generated'].items():
        assert digest((ROOT / path).read_bytes()) == expected, path
    generated = (ROOT / 'kapi/kapi_generated.c').read_text()
    wrappers = ''
    for name, expected in fixture['wrappers'].items():
        body = function(generated, 'wrap_' + name)
        target = 'platform_' + name
        if name != 'pci_bind_info':
            entry = next(e for e in spec['api'] if e['name'] == name)
            assert entry['target'] == target
            assert target + '(' in body
            assert body.replace(target + '(', name + '(') == expected, name
        else:
            assert body == expected
        wrappers += body + '\n'
    diagnostic = function((ROOT / 'kapi/kapi_sys.c').read_text(), 'kapi_pci_bind_info')
    assert diagnostic.replace('platform_pci_bind_info_get(', 'pci_bind_info_get(') == fixture['diagnostic']
    wrappers = diagnostic + '\n' + wrappers
    assert 'pci.h' not in spec['includes'] and 'pci_dev_contract.h' in spec['includes']
    assert '#include "pci_bind.h"' not in (ROOT / 'kapi/kapi_sys.c').read_text()
    assert_hwdev_contract(ROOT)
    print('PASS fixed baseline public ABI/generated files, wrapper guards/snapshot, A4/B5 and hwdev CRC')
    return wrappers


def build(folder, body, wrappers):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'platform/pc98/device_pci.c').write_text(body)
    (folder / 'wrappers.inc').write_text(wrappers)
    exe = folder / 'pci-test'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector',
                    *['-I' + str(folder / p) for p in ('.', 'include', 'drivers', 'sdk/include/os32')],
                    str(folder / 'platform/pc98/device_pci.c'),
                    str(folder / 'tools/tests/dep_dir_a_l08_host.c'), '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    wrappers = contracts()
    body = (ROOT / 'platform/pc98/device_pci.c').read_text()
    assert body.count(OLD) == 1
    with tempfile.TemporaryDirectory(prefix='dep-dir-a-l08-') as directory:
        folder = Path(directory)
        subprocess.run(['qemu-i386', str(build(folder / 'control', body, wrappers))], check=True)
        print('PASS four forwards, idx/config edges, 40B/8B copies, local snapshot, absent and invalid outputs')
        if args.prepare:
            build(folder / 'prepared', body.replace(OLD, NEW), wrappers)
            print('PASS copied headers only; unique anchor; substituted copy builds -Werror (not run)')
        if args.mutate:
            result = subprocess.run(['qemu-i386', str(build(folder / 'mutant', body.replace(OLD, NEW), wrappers))])
            assert result.returncode == 1
            print('RED runtime: wrong config register (build succeeded)')


if __name__ == '__main__':
    main()
