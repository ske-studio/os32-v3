#!/usr/bin/env python3
"""Production boot assembly starts exactly one shared NIC (ILP32/qemu)."""
CHECK = dict(name='hostsvc-boot', tier=1, needs=['make:all'], budget=5.2, covers=['tools/tests/rx_io_clock.h', 'tools/tests/idle_io_sources.py',
    'kernel/io_work.[ch]', 'sys/kern/io_clock.h', 'kernel/io_policy.[ch]', 'sys/kern/idle_work.h', 'sys/kern/exec/appslot.h',
    'net/rx_work.[ch]', 'sys/hal/time/tick_clock.h', 'tools/tests/link_platform_fake/platform_link_inline.h', 'sys/kern/idle_work.h', 'sys/platform/pc98/boot_capture.c', 'sys/platform/pc98/bootinfo_wire.h', 'sys/platform/pc98/bootinfo_native.h',
    'sys/dev/net/e82557/lan_body.c',
    'sys/dev/block/netcd.h',
    'sys/hal/block/dev.h',
    'sys/fs/serialfs/sfs_block.h',
    'sys/dev/net/e82557/lan_module.c',
    'sys/dev/net/e82557/lan_module.h',
    'sys/dev/net/e82557/e82557.h',
    'sys/dev/net/e82557/e82557_regs.h',
    'sys/dev/net/ne2000/lgy98.c',
    'sys/dev/net/ne2000/lgy98.h',
    'sys/dev/net/ne2000/ne2000.h',
    'sys/dev/net/ne2000/ne2000_regs.h',
    'sys/dev/char/np2sysp.h',
    'sys/net/nic.c',
    'sys/net/nic.h',
    'sys/net/nic_rx.h',
    'sys/net/rx_work.h',
    'sys/dev/net/e82557/nic_e82557.c',
    'sys/dev/net/ne2000/nic_lgy98.c',
    'sys/platform/pc98/module_pair.c',
    'sys/net/link.h', 'sys/hal/net/link_platform.h', 'sys/platform/pc98/platform_link_inline.h', 'sys/platform/pc98/link_platform.c', 'sys/net/link.c', 'sys/net/link_notify.h', 'sys/net/link_host_api.inc',
    'sys/net/link_rx.inc', 'sys/net/link_selftest.inc', 'lib/le_internal.h',
    'sys/kern/irq/tick.h',
    'sys/kern/irq/irq.h',
    'sys/platform/pc98/tick_order.h',
    'sys/kern/module/module.h',
    'sys/kern/module/module_runtime.h',
    'sys/kern/module/module_image.h',
    'sys/kern/module/module_schema.h',
    'sys/include/types.h',
    'sys/include/config.h',
    'sys/include/kprintf.h',
    'sys/include/bootinfo.h',
    'sys/include/net_eth.h',
    'sys/include/lan_contract.h',
    'sys/include/pci_bind_contract.h',
    'sys/include/pci_dev_contract.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_generations.h',
    'sys/kern/kstring.h',
    'sys/kern/mm/dma_pool.h',
    'sys/kern/mm/pgalloc.h',
    'sys/dev/pci/pci.h',
    'sys/dev/pci/pci_bind.h',
    'sys/dev/pci/pci_decode.h',
    'sys/fs/serialfs/sfs_lan.h',
    'sys/fs/serialfs/sfs_client.h',
    'sys/fs/serialfs/sfs_proto.h',
    'tools/prototypes/lan_imports.def',
    'build/out/lan/lan_imports.h',
    'build/config.mk',
    'sys/platform/pc98/kernel.c',
    'sys/platform/pc98/kernel_boot_memory.inc',
    'sys/platform/pc98/boot.c',
    'tools/tests/hostsvc_boot_host.c',
    'tools/tests/hostsvc_lgy98_attach_host.c',
    'tools/tests/hostsvc_fixture.h',
    'tools/tests/hostsvc_fixture.c',
    'tools/tests/host32.py',
    'tools/tests/kernel_includes.py',
    'tools/tests/kernel_boot_source.py',
],
    basis=['docs/design/drivers/block/netcd.md 起動の手順と設定'],
    pins=['LAN_BULK_DESIGN'],
)
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
from kernel_boot_source import kernel_source
from idle_io_sources import idle_io_sources

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['sys/dev/net/e82557/lan_body.c', 'sys/dev/net/e82557/lan_module.c',
           'sys/dev/net/ne2000/lgy98.c', 'sys/net/nic.c']
MUTATIONS = [
    ('pin-auto-peer', 3, 'fixed_peer = 1; /* Pin AUTO without restarting the link. */', 'fixed_peer = 0;'),
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
                    '-I' + str(ROOT / 'drivers/net/ne2000'), '-I' + str(ROOT / 'tools/tests/link_platform_fake'), *include_flags(ROOT),
                    '-DLAN_BODY_SOURCE="' + str(paths[0]) + '"',
                    '-DLAN_RESIDENT_SOURCE="' + str(paths[1]) + '"',
                    '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                    str(ROOT / 'tools/tests/hostsvc_boot_host.c'), str(paths[2]),
                    str(paths[3]), str(ROOT / 'sys/dev/net/e82557/nic_e82557.c'),
                    str(ROOT / 'sys/dev/net/ne2000/nic_lgy98.c'), str(ROOT / 'sys/platform/pc98/module_pair.c'), str(ROOT / 'tools/tests/hostsvc_fixture.c'),
                    str(ROOT / 'sys/net/rx_work.c'), *idle_io_sources(ROOT, work, compat=False), '-o', str(exe)], check=True)
    return [host32.run([str(exe)], input=bytes([mode]), capture_output=True).returncode
            for mode in (1, 2, 3, 4, 5, 6, 7)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    kernel = kernel_source(ROOT)
    calls = [m.start() for m in re.finditer(r'kernel_boot_lan\(&boot\);', kernel)]
    assert len(calls) == 2
    assert max(calls) < kernel.index('boot_platform_run(BOOT_NIC_LEGACY, &boot);')
    boot = (ROOT / 'sys/platform/pc98/boot.c').read_text()
    assert '[BOOT_NIC_LEGACY] = pc98_boot_nic_legacy' in boot
    assert 'lgy98_init();' in boot
    originals = [(ROOT / path).read_text() for path in SOURCES]
    with tempfile.TemporaryDirectory(prefix='hostsvc-boot-') as tmp:
        work = Path(tmp)
        (work / 'io.h').write_text('#ifndef BOOT_TEST_IO_H\n#define BOOT_TEST_IO_H\n'
            'static inline void _idle(void) {}\nint _irq_enabled(void);\n'
            'unsigned int boot_irq_save(void);\n'
            'void boot_irq_restore(unsigned int);\n'
            '#define irq_save boot_irq_save\n#define irq_restore boot_irq_restore\n#endif\n')
        results = build_run(work, originals)
        if any(results):
            raise RuntimeError(f'control runtime rc={results}')
        print('GREEN boot: 82557 only / LGY only / both / missing lan.mod / configured peer (pending, same, different)', flush=True)
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
