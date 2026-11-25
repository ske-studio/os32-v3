"""L08 PCI diagnostic KAPI boundary; --prepare builds but never runs a mutation."""
CHECK = dict(name='dep-dir-a-l08-host', tier=1,
    covers=['sys/platform/pc98/gfx_surfaces.c', 'sys/include/callconv.h', 'sys/platform/pc98/bios_snapshot.h', 'sys/platform/pc98/boot_capture.c', 'sys/platform/pc98/bootinfo_wire.h', 'sys/platform/pc98/bootinfo_native.h', 'sys/platform/pc98/legacy/lowmap.c', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'tools/hwdev_layout.json', 'sys/dev/char/serial.h', 'sys/dev/hwdev/hwdev_imports.def', 'sys/include/bootinfo.h', 'sys/include/gf_production.h', 'sys/include/kprintf.h', 'sys/platform/pc98/kdebug/kpeek.h', 'sys/dev/video/geforce/diag/recover.h', 'sys/kern/module/kdebug_imports.def', 'sys/kern/mm/pgalloc.h', 'sys/kern/kstring.h', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/module_schema.py', 'tools/tests/hwdev_contract_fixture.py', 'sys/include/memmap.h', 'sys/include/platform_device.h', 'sys/include/time_units.h', 'sys/include/types.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_snd2.h', 'sdk/include/os32/os32_in2.h', 'sdk/include/os32/os32_v86s.h', 'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_gfx2.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h', 'sdk/kapi.json', 'sys/kapi/kapi_generated.c', 'tools/tests/test_dep_dir_a_l05a.py', 'tools/tests/fixtures/dep_dir_a/l05a.json', 'tools/tests/test_shm_user.py', 'tools/tests/kernel_layout.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/rust/os32api/src/kapi_generated.rs', 'sys/dev/hwdev/generated/hwdev_contract.h', 'sys/kern/exec/exec.c', 'sys/platform/pc98/video/backend_pegc.c', 'sys/hal/gfx/gfx_core.c', 'sys/platform/pc98/kernel.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/physmem.c', 'sys/kern/mm/shm.c', 'sys/platform/pc98/legacy/v86.c', 'sys/platform/pc98/legacy/v86_mem.c', 'sys/platform/pc98/bios_snapshot.c', 'tools/tests/shm_user_host.c', 'sys/include/pci_dev_contract.h', 'sys/include/pci_contract.h', 'sys/include/pci_bind_contract.h', 'sys/dev/pci/device_pci.c', 'sys/kern/exec/exec.h', 'sys/include/user_read_contract.h', 'tools/tests/dep_dir_a_l08_host.c', 'tools/tests/fixtures/dep_dir_a/l08.json', 'sys/dev/ide/device_ide.c', 'tools/tests/dep_dir_a_l05a_host.c', 'sys/dev/hwdev/generated/hwdev_imports.json', 'sys/dev/hwdev/generated/hwdev_layout.txt', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=3.1,
    basis=['[ABI]', 'KAPI_SPEC §3-1'],
)

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
COPIES = ('sys/include/callconv.h', 'sys/kern/exec/exec.h', 'sys/include/user_read_contract.h',
    'sys/include/types.h', 'sys/include/platform_device.h', 'sys/include/pci_dev_contract.h',
    'sys/include/pci_contract.h', 'sys/include/pci_bind_contract.h',
    'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_snd2.h', 'sdk/include/os32/os32_in2.h', 'sdk/include/os32/os32_v86s.h', 'sdk/include/os32/os32_host_notice.h',
    'sys/include/memmap.h', 'sys/include/time_units.h',
    'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_generations.h',
    'sdk/include/os32/os32_gfx2.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'tools/tests/dep_dir_a_l08_host.c', 'sys/dev/pci/device_pci.c',
)
OLD = 'return pci_cfg_read32(bus, dev, fn, reg);'
NEW = 'return pci_cfg_read32(bus, dev, fn, reg ^ 4u);'



def contracts():
    fixture = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l08.json').read_text())
    abi = json.loads((ROOT / 'tools/tests/fixtures/dep_dir_a/l05a.json').read_text())
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    generated = (ROOT / 'sys/kapi/kapi_generated.c').read_text()
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
    diagnostic = function((ROOT / 'sys/dev/pci/device_pci.c').read_text(), 'kapi_pci_bind_info')
    assert diagnostic.replace('platform_pci_bind_info_get(', 'pci_bind_info_get(') == fixture['diagnostic']
    assert 'pci.h' not in spec['includes'] and 'pci_dev_contract.h' in spec['includes']
    assert '#include "pci_bind.h"' not in (ROOT / 'sys/dev/pci/device_pci.c').read_text()
    # Private GF values follow the generated pair; public ABI snapshots stay fixed.
    assert_hwdev_contract(ROOT)
    print('PASS fixed baseline public ABI/generated files, wrapper guards/snapshot, A4/B5 and current hwdev pair')
    return wrappers


def build(folder, body, wrappers):
    for rel in COPIES:
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dest)
    (folder / 'sys/dev/pci/device_pci.c').write_text(body)
    (folder / 'wrappers.inc').write_text(wrappers)
    exe = folder / 'pci-test'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector',
                    *include_flags(folder),
                    str(folder / 'sys/dev/pci/device_pci.c'),
                    str(folder / 'tools/tests/dep_dir_a_l08_host.c'), '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    wrappers = contracts()
    body = (ROOT / 'sys/dev/pci/device_pci.c').read_text()
    assert body.count(OLD) == 1
    with tempfile.TemporaryDirectory(prefix='dep-dir-a-l08-') as directory:
        folder = Path(directory)
        subprocess.run(['qemu-i386', str(build(folder / 'control', body, wrappers))], check=True)
        print('PASS four forwards, idx/config edges, 40B/8B copies, local snapshot, absent and invalid outputs')
        if args.prepare:
            build(folder / 'prepared', body.replace(OLD, NEW), wrappers)
            print('PASS copied headers only; unique anchor; substituted copy builds -Werror (not run)')
        if args.mutate:
            copy = 'i < PCI_BIND_INFO_SIZE; i++'
            assert body.count(copy) == 1
            short = body.replace(copy, 'i < PCI_BIND_INFO_SIZE - 1; i++')
            result = subprocess.run(['qemu-i386', str(build(folder / 'short-copy', short, wrappers))])
            assert result.returncode == 1
            print('RED runtime: short bind info copy (build succeeded)')
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
