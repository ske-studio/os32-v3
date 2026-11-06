#!/usr/bin/env python3
"""Production boot assembly starts exactly one shared NIC (ILP32/qemu)."""
CHECK = dict(name='hostsvc-boot', tier=1, needs=['make:all'], budget=5, covers=[
    'drivers/net/e82557/lan_body.c',
    'drivers/net/e82557/lan_module.c',
    'drivers/net/e82557/lan_module.h',
    'drivers/net/e82557/e82557.h',
    'drivers/net/e82557/e82557_regs.h',
    'drivers/net/ne2000/lgy98.c',
    'drivers/net/ne2000/lgy98.h',
    'drivers/net/ne2000/ne2000.h',
    'drivers/net/ne2000/ne2000_regs.h',
    'drivers/char/np2sysp.h',
    'net/nic.c',
    'net/nic.h',
    'net/nic_rx.h',
    'net/nic_e82557.c',
    'net/nic_lgy98.c',
    'net/link.h',
    'kernel/irq/tick.h',
    'kernel/irq/irq.h',
    'platform/pc98/tick_order.h',
    'kernel/module/module.h',
    'kernel/module/module_runtime.h',
    'kernel/module/module_image.h',
    'kernel/module/module_schema.h',
    'include/types.h',
    'include/config.h',
    'include/kprintf.h',
    'include/bootinfo.h',
    'include/net_eth.h',
    'include/lan_contract.h',
    'include/pci_bind_contract.h',
    'include/pci_dev_contract.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_generations.h',
    'lib/kstring.h',
    'mm/dma_pool.h',
    'mm/pgalloc.h',
    'drivers/pci/pci.h',
    'drivers/pci/pci_bind.h',
    'drivers/pci/pci_decode.h',
    'fs/serialfs/sfs_lan.h',
    'fs/serialfs/sfs_client.h',
    'fs/serialfs/sfs_proto.h',
    'tools/prototypes/lan_imports.def',
    'build/out/lan/lan_imports.h',
    'build/config.mk',
    'init/kernel.c',
    'init/kernel_boot_memory.inc',
    'platform/pc98/boot.c',
    'tools/tests/hostsvc_boot_host.c',
    'tools/tests/hostsvc_lgy98_attach_host.c',
    'tools/tests/hostsvc_fixture.h',
    'tools/tests/hostsvc_fixture.c',
    'tools/tests/host32.py',
    'tools/tests/kernel_includes.py',
    'tools/tests/kernel_boot_source.py',
])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
from kernel_boot_source import kernel_source

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['drivers/net/e82557/lan_body.c', 'drivers/net/e82557/lan_module.c',
           'drivers/net/ne2000/lgy98.c']
MUTATIONS = [
    ('body-adapter', 0, '.adapter = &net_e82557_adapter,', '.adapter = 0,'),
    ('82557-registration', 1, 'rc = net_adapter_register(NET_NIC_E82557, candidate.adapter);', 'rc = 0;'),
    ('82557-start', 1, 'if (!rc) rc = net_start(NET_NIC_AUTO, NULL);', 'if (!rc) rc = 0;'),
    ('lgy-start', 2, 'rc = net_start(NET_NIC_AUTO, NULL);', 'rc = 0;'),
]

def build_run(work, sources):
    paths = []
    for name, source in zip(SOURCES, sources):
        path = work / Path(name).name
        path.write_text(source)
        paths.append(path)
    exe = work / 'boot'
    flags = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-function', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
             '-ffunction-sections', '-fdata-sections', '-D__KERNEL_BUILD__']
    subprocess.run(['gcc', *flags, '-I' + str(work), '-I' + str(ROOT),
                    '-I' + str(ROOT / 'tools/tests'), '-I' + str(ROOT / 'build/out/lan'),
                    '-I' + str(ROOT / 'drivers/net/ne2000'), *include_flags(ROOT),
                    '-DLAN_BODY_SOURCE="' + str(paths[0]) + '"',
                    '-DLAN_RESIDENT_SOURCE="' + str(paths[1]) + '"',
                    '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                    str(ROOT / 'tools/tests/hostsvc_boot_host.c'), str(paths[2]),
                    str(ROOT / 'net/nic.c'), str(ROOT / 'net/nic_e82557.c'),
                    str(ROOT / 'net/nic_lgy98.c'), str(ROOT / 'tools/tests/hostsvc_fixture.c'),
                    '-o', str(exe)], check=True)
    return [host32.run([str(exe)], input=bytes([mode]), capture_output=True).returncode
            for mode in (1, 2, 3, 4)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    kernel = kernel_source(ROOT)
    assert kernel.index('lan_module_load();') < kernel.index('boot_platform_run(BOOT_NIC_LEGACY, &boot);')
    boot = (ROOT / 'platform/pc98/boot.c').read_text()
    assert '[BOOT_NIC_LEGACY] = pc98_boot_nic_legacy' in boot
    assert 'lgy98_init();' in boot
    originals = [(ROOT / path).read_text() for path in SOURCES]
    with tempfile.TemporaryDirectory(prefix='hostsvc-boot-') as tmp:
        work = Path(tmp)
        (work / 'io.h').write_text('unsigned int boot_irq_save(void);\n'
            'void boot_irq_restore(unsigned int);\n'
            '#define irq_save boot_irq_save\n#define irq_restore boot_irq_restore\n')
        results = build_run(work, originals)
        if any(results):
            raise RuntimeError(f'control runtime rc={results}')
        print('GREEN boot: 82557 only / LGY only / both / missing lan.mod', flush=True)
        if args.mutate:
            for name, index, before, after in MUTATIONS:
                assert originals[index].count(before) == 1, name
                sources = originals.copy()
                sources[index] = sources[index].replace(before, after)
                results = build_run(work, sources)
                if not any(rc > 0 for rc in results) or any(rc < 0 for rc in results):
                    raise RuntimeError(f'{name}: expected runtime RED: {results}')
                print(f'RED {name}: runtime rc={results}', flush=True)
    return 0

def run(ctx):
    for runner in ctx.runners:
        env = dict(os.environ, HOST32_RUNNERS=runner)
        rc = subprocess.call(['python3', '-B', 'tools/tests/test_hostsvc_boot.py',
                              *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)
        if rc:
            return rc
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
