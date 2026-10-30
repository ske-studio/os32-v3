"""L02 resident match wiring, fixed PCI layout and contract regeneration.

--prepare compiles copied substitutions only; it does not execute mutants.
"""
CHECK = dict(name='dep-dir-a-l02-host', tier=1,
    covers=['drivers/char/serial.h', 'hwdev/hwdev_imports.def', 'include/gf_production.h', 'kdebug/kpeek.h', 'kdebug/recover.h', 'tools/tests/hwdev_contract_fixture.py', 'kernel/irq/tick.c', 'kernel/irq/tick.h', 'platform/pc98/tick_order.h', 'kernel/module/module_runtime.c', 'kernel/module/module_runtime.h', 'kernel/module/module_image.c', 'kernel/module/module_image.h', 'kernel/module/module_schema.h', 'mm/pgalloc.h', 'include/types.h', 'include/memmap.h', 'fs/vfs.c', 'fs/vfs.h', 'drivers/base/dev.c', 'drivers/base/dev.h', 'drivers/block/disk.h', 'drivers/ata/atapi.h', 'drivers/block/fdc.h', 'include/console.h', 'include/kbd.h', 'drivers/input/kbd.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'lib/kstring.h', 'lib/crc32_core.inc', 'tools/tests/module_runtime_host.c', 'tools/tests/test_module_runtime.py', 'tools/tests/module_runtime_tdd.md', 'tools/tests/test_module_image.py', 'tools/tests/module_image_host.c', 'tools/tests/host32.py', 'drivers/net/e82557/lan_module.c', 'drivers/net/e82557/lan_module.h', 'drivers/net/e82557/lan_body.c', 'init/kernel.c', 'include/config.h', 'sdk/include/os32/os32_generations.h', 'include/bootinfo.h', 'tools/prototypes/lan_imports.def', 'tools/gen_sqlite_imports.py', 'tools/tests/lan_module_host.c', 'tools/tests/test_lan_module.py', 'build/lan-module.ld', 'drivers/net/e82557/e82557.h', 'drivers/net/e82557/e82557_regs.h', 'drivers/pci/pci.h', 'drivers/pci/pci_decode.h', 'drivers/pci/pci_bind.c', 'drivers/pci/pci_bind_match.c', 'drivers/pci/pci_bind.h', 'kernel/irq/irq.h', 'kernel/irq/irq_math.h', 'mm/dma_pool.h', 'kernel/module/module.h', 'include/sys.h', 'include/kprintf.h', 'kernel/module/kdebug_imports.def', 'fs/serialfs/sfs_lan.h', 'fs/serialfs/sfs_client.h', 'fs/serialfs/sfs_proto.h', 'tools/mkmod.py', 'tools/gen_kdebug_imports.py', 'tools/mkpkg.py', 'tools/deploy_manifests.py', 'build/core.yaml', 'build/packages.yaml', 'userland/deploy.yaml', 'sdk/kapi.json', 'build/out/lan/*', 'build/out/lan.mod', 'build/out/vmkernel.lz4', 'build/out/kernel.elf', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'include/lan_contract.h', 'include/dma_contract.h', 'include/pci_dev_contract.h', 'include/pci_bind_contract.h', 'platform/pc98/lan_match.c', 'tools/tests/dep_dir_a_l02_host.c', 'tools/tests/test_e82557_drv.py', 'tools/tests/e82557_drv_host.c', 'tools/hwdev_layout.json', 'hwdev/generated/hwdev_contract.h', 'build/lan-module.mk', 'tools/tests/fixtures/dep_dir_a/l02.json', 'drivers/net/e82557/e82557.c', 'drivers/net/e82557/e82557_probe_reset_base.inc', 'drivers/net/e82557/e82557_desc.c', 'drivers/net/e82557/e82557_eeprom.c', 'drivers/pci/pci_decode.c', 'hwdev/generated/hwdev_imports.json', 'hwdev/generated/hwdev_layout.txt', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc', 'lib/sqlite3/os32_sqlite_vfs.c', 'tools/prototypes/sqlite_imports.def'],
    needs=['make:all'], budget=60.0)

from kernel_includes import include_flags
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
          'include/types.h', 'drivers/pci/pci_decode.h', 'drivers/pci/pci.h',
          'drivers/pci/pci_bind.h', 'drivers/pci/pci_bind_match.c', 'drivers/net/e82557/lan_module.h',
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
                    *include_flags(folder),
                    *include_flags(ROOT),
                    str(folder / 'tools/tests/dep_dir_a_l02_host.c'),
                    str(folder / 'platform/pc98/lan_match.c'),
                    str(folder / 'drivers/pci/pci_bind_match.c'), '-o', str(exe)], check=True)
    return exe


def contracts():
    for header in ('include/pci_dev_contract.h', 'include/pci_bind_contract.h'):
        result = subprocess.run(['make', '-n', '-j1', '-W', header,
                                 'build/out/lan/lan_imports.h'], cwd=ROOT,
                                capture_output=True, text=True, check=True)
        assert 'gen_sqlite_imports.py --module lan ' in result.stdout, result.stdout
    assert hashlib.sha256((ROOT / 'tools/hwdev_layout.json').read_bytes()).hexdigest() == HWDEV_SHA
    # Private GF values follow the generated pair; public ABI snapshots stay fixed.
    assert_hwdev_contract(ROOT)
    print('PASS B1/B2: fixed hwdev inventory/current private pair, both LAN contract dependencies')


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


HWDEV_SHA = '69145f216adceb97e217b9d4b10e8731e04fbc57ba0dbb6c1b868cedca6170b5'
if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_a_l02.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
