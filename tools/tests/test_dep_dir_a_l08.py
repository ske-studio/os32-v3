"""L08 PCI diagnostic KAPI boundary; --prepare builds but never runs a mutation."""
CHECK = dict(name='dep-dir-a-l08-host', tier=1,
    covers=['drivers/char/serial.h', 'hwdev/hwdev_imports.def', 'include/bootinfo.h', 'include/gf_production.h', 'include/kprintf.h', 'kdebug/kpeek.h', 'kdebug/recover.h', 'kernel/module/kdebug_imports.def', 'mm/pgalloc.h', 'lib/kstring.h', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/module_schema.py', 'tools/tests/hwdev_contract_fixture.py', 'include/memmap.h', 'include/platform_device.h', 'include/time_units.h', 'include/types.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h', 'sdk/kapi.json', 'kapi/kapi_generated.c', 'tools/tests/test_dep_dir_a_l05a.py', 'tools/tests/fixtures/dep_dir_a/l05a.json', 'tools/tests/test_shm_user.py', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/rust/os32api/src/kapi_generated.rs', 'hwdev/generated/hwdev_contract.h', 'exec/exec.c', 'drivers/video/pc98/backend_pegc.c', 'gfx/gfx_core.c', 'init/kernel.c', 'mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shm.c', 'kernel/v86/v86.c', 'kernel/v86/v86_mem.c', 'kernel/v86/v86_snapshot.c', 'tools/tests/shm_user_host.c', 'include/pci_dev_contract.h', 'include/pci_bind_contract.h', 'drivers/pci/pci.h', 'drivers/pci/pci_decode.h', 'drivers/pci/pci_bind.h', 'platform/pc98/device_pci.c', 'kapi/kapi_sys.c', 'tools/tests/dep_dir_a_l08_host.c', 'tools/tests/fixtures/dep_dir_a/l08.json', 'platform/pc98/device_ide.c', 'tools/tests/dep_dir_a_l05a_host.c', 'hwdev/generated/hwdev_imports.json', 'hwdev/generated/hwdev_layout.txt', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py', 'tools/tests/paging_source.py'],
    needs=['make:all'], budget=60.0)

from kernel_includes import include_flags
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
    'include/pci_bind_contract.h', 'drivers/pci/pci.h', 'drivers/pci/pci_decode.h',
    'drivers/pci/pci_bind.h', 'sdk/include/os32/os32_kapi_shared.h',
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
    # Private GF values follow the generated pair; public ABI snapshots stay fixed.
    assert_hwdev_contract(ROOT)
    print('PASS fixed baseline public ABI/generated files, wrapper guards/snapshot, A4/B5 and current hwdev pair')
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
                    *include_flags(folder),
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


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_a_l08.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
