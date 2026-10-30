"""T5c-4 real resident LAN adapter/runtime under ILP32; device calls are seams.

LB's real driver/PCI and SerialFS transport regressions remain separate checks.
This suite establishes the owner lifetime across those callbacks, not NIC I/O.
"""
CHECK = dict(name='lan-module-host', tier=1,
    covers=['kernel/irq/tick.c', 'kernel/irq/tick.h', 'platform/pc98/tick_order.h', 'kernel/module/module_runtime.c', 'kernel/module/module_runtime.h', 'kernel/module/module_image.c', 'kernel/module/module_image.h', 'kernel/module/module_schema.h', 'mm/pgalloc.h', 'include/types.h', 'include/memmap.h', 'fs/vfs.c', 'fs/vfs.h', 'drivers/base/dev.c', 'drivers/base/dev.h', 'drivers/block/disk.h', 'drivers/ata/atapi.h', 'drivers/block/fdc.h', 'include/console.h', 'include/kbd.h', 'drivers/input/kbd.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'lib/kstring.h', 'lib/crc32_core.inc', 'tools/tests/module_runtime_host.c', 'tools/tests/test_module_runtime.py', 'tools/tests/module_runtime_tdd.md', 'tools/tests/test_module_image.py', 'tools/tests/module_image_host.c', 'tools/tests/host32.py', 'build/checks.d/check-module-runtime-host.mk', 'tools/check_map.d/check-module-runtime-host.yaml', 'drivers/net/e82557/lan_module.c', 'drivers/net/e82557/lan_module.h', 'drivers/net/e82557/lan_body.c', 'init/kernel.c', 'include/config.h', 'sdk/include/os32/os32_generations.h', 'include/bootinfo.h', 'tools/prototypes/lan_imports.def', 'tools/gen_sqlite_imports.py', 'tools/tests/lan_module_host.c', 'tools/check_map.d/check-lan-module-host.yaml', 'build/checks.d/check-lan-module-host.mk', 'build/lan-module.ld', 'drivers/net/e82557/e82557.h', 'drivers/net/e82557/e82557_regs.h', 'drivers/pci/pci.h', 'drivers/pci/pci_decode.h', 'drivers/pci/pci_bind.c', 'drivers/pci/pci_bind_match.c', 'drivers/pci/pci_bind.h', 'kernel/irq/irq.h', 'kernel/irq/irq_math.h', 'mm/dma_pool.h', 'kernel/module/module.h', 'include/sys.h', 'include/kprintf.h', 'kernel/module/kdebug_imports.def', 'fs/serialfs/sfs_lan.h', 'fs/serialfs/sfs_client.h', 'fs/serialfs/sfs_proto.h', 'tools/mkmod.py', 'tools/gen_kdebug_imports.py', 'tools/mkpkg.py', 'tools/deploy_manifests.py', 'build/core.yaml', 'build/packages.yaml', 'userland/deploy.yaml', 'sdk/kapi.json', 'build/out/lan/*', 'build/out/lan.mod', 'build/out/vmkernel.lz4', 'build/out/kernel.elf', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'include/lan_contract.h', 'include/dma_contract.h', 'include/pci_dev_contract.h', 'include/pci_bind_contract.h', 'platform/pc98/lan_match.c', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc'],
    needs=['make:all'], budget=4.424432)

from kernel_boot_source import kernel_source
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

import host32
from test_module_runtime import FLAGS, INCLUDES, image
from test_module_image import build_validator, validate, fnv, crc, word

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'drivers/net/e82557/lan_module.c'
CASES = ['normal-detach', 'decline-zero-refs', 'quarantine-no-rebind-free',
         'active-session-retained', 'prefix-failure', 'failure-after-bind',
         'decline-after-irq-dma-cleanup', 'missing-module-pci-info', 'init-failure-pci-info']


def product_image(folder):
    """Real packed image, imports, paired rejection, and resident facade."""
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    metadata = json.loads((ROOT / 'build/out/lan/lan_imports.json').read_text())
    data = (ROOT / 'build/out/lan.mod').read_bytes()
    kernel = (ROOT / 'build/out/vmkernel.lz4').read_bytes()
    sys.path.insert(0, str(ROOT / 'tools'))
    import mkmod
    pair = mkmod.pair_crc(kernel)
    rebuilt, _ = mkmod.build((ROOT / 'build/out/lan/lan.elf').read_bytes(), metadata,
                             kernel, spec['version'], spec['generations']['memory_layout'],
                             module_id=9)
    assert rebuilt == data
    names = {entry['name'] for entry in metadata['imports']}
    assert {'lan_module_irq_register', 'lan_module_irq_unregister',
            'lan_module_dma_alloc', 'lan_module_dma_free', 'dma_mark_leaked',
            'ktime_ticks'} <= names
    assert not {'irq_register', 'irq_unregister', 'dma_alloc', 'dma_free',
                'tick_count', 'kdebug'} & names
    core = mkmod.Elf((ROOT / 'build/out/kernel.elf').read_bytes())
    body = mkmod.Elf((ROOT / 'build/out/lan/lan.elf').read_bytes())
    for name in ('lan_module_init', 'lan_module_quiet', 'lan_module_release',
                 'e82557_tcb_build', 'e82557_ee_read_word'):
        assert name not in core.symbols and name in body.symbols, name
    for name in ('e82557_send', 'e82557_recv', 'e82557_tick', 'e82557_state',
                 'e82557_get_mac', 'e82557_driver', 'sfs_lan_init', 'sfs_lan_io',
                 'sfs_lan_reset', 'sfs_lan_set_peer'):
        assert name in core.symbols and name in body.symbols, name
    # h4 uses the resident VFS/PCI/DMA facades and keeps its existing contracts.
    imports = (ROOT / 'kernel/module/kdebug_imports.def').read_text()
    for name in ('dma_alloc', 'dma_free', 'vfs_read'):
        assert name in core.symbols and '"symbol": "' + name + '"' in imports, name
    for name in ('pci_bind_all', 'pci_cfg_read32', 'irq_register', 'vfs_read'):
        assert name in core.symbols, name
    exe = build_validator(folder)
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=metadata['module_abi_gen'], pair=pair, ops=1, img_valid=1, module_id=9,
                    imports=[(fnv(e['name']), e['type_ver'], e['kind']) for e in metadata['imports']],
                    exports=[(1, fnv('initialize'), 1)])
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        def check(payload, rc):
            result = validate(exe, payload, relocate_base=0x1200000, runner=runner, **contract)
            assert result.returncode == bool(rc), result
            assert result.stdout == (b'GREEN\n' if not rc else f'RED {rc:02}\n'.encode()), result
        check(data, 0)
        for field, value, rc in ((9, 0, 3), (31, pair ^ 1, 4), (8, 1, 3)):
            bad = bytearray(data)
            word(bad, field, value)
            check(crc(bad), rc)
        check(data[:-1], 3)
        bad = bytearray(data)
        bad[-1] ^= 1
        check(bad, 9)
    print('PASS product LAN: paired POST_ROOT, relocate/reject, body moved, core facades', flush=True)


def boot_distribution():
    """FD plan and boot placement are independent of optional service modes."""
    import mkpkg
    kernel = kernel_source(ROOT)
    begin = kernel.index('services_stage = SERVICES_ROOT_READY;')
    end = kernel.index('services_boot();', begin)
    boot = kernel[begin:end]
    assert kernel.count('lan_module_load();') == 1
    assert re.search(r'if \(memory_boot_dma_available\(\)\)\s*\{\s*lan_module_load\(\);\s*\}', boot)
    assert boot.index('lan_module_pci_bind_fallback();') > boot.index('lan_module_load();')
    assert 'services_boot_mode' not in boot and 'SYS_LAN_MOD' in (ROOT / 'include/config.h').read_text()
    plan = mkpkg.load_plan(ROOT / 'build/packages.yaml')
    for loader in ('boot/loader_fat_new.bin', 'boot/loader_fat144.bin'):
        entries, problems = mkpkg.fd_plan(plan, str(ROOT), loader)
        assert not problems, problems
        assert ('/boot/lan.mod', 'build/out/lan.mod') in entries
    print('PASS FD LAN distribution and ROOT_READY load before optional services', flush=True)


def runtime_checks(folder, mutations=False):
    exe = folder / 'lan-module'

    def build(source):
        subprocess.run(['gcc', *FLAGS, '-Wno-unused-parameter', *INCLUDES,
                        '-I' + str(ROOT / 'build/out/lan'),
                        '-DLAN_ADAPTER="' + str(source) + '"',
                        '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                        ROOT / 'tools/tests/lan_module_host.c',
                        ROOT / 'kernel/module/module_runtime.c', ROOT / 'kernel/module/module_image.c',
                        ROOT / 'platform/pc98/lan_match.c',
                        ROOT / 'fs/vfs.c', ROOT / 'drivers/base/dev.c', '-o', exe], check=True)

    def run(modes, expected=0):
        for mode in modes:
            payload = struct.pack('<2I', 10 if mode == 4 else 0, mode) + image()
            for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
                assert result.returncode == expected, (CASES[mode], runner, result)
                if expected:
                    assert b'FAIL ' in result.stdout, result
            print(('MUTATE RED ' if expected else 'PASS ') + CASES[mode], flush=True)

    build(SOURCE)
    run(range(len(CASES)))
    if mutations:
        source = SOURCE.read_text()
        for label, old, new in (
            ('pci-fallback', '        pci_bind_all(NULL, 0);', '        /* omit fallback */'),
            ('pci-rebind', '    if (!pci_bound) {', '    if (1) {'),
            ('tick-detach',
             'if (!quiet_done || r->active_refs) return 0;\n    lan_detach_tick();',
             'if (!quiet_done || r->active_refs) return 0;\n    /* omit tick detach */'),
            ('irq-detach',
             'int rc = !irq_target || !lan_module_irq_unregister(irq_line, irq_target, 0);',
             'int rc = 1; /* omit IRQ detach */'),
        ):
            assert source.count(old) == 1, label
            mutant = folder / (label + '.c')
            mutant.write_text(source.replace(old, new))
            build(mutant)
            run([7, 8] if label == 'pci-fallback' else [0, 5], expected=1)
            print('MUTATE ' + label + ' RED (compiled, runtime assertions)', flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='lan-module-') as td:
        folder = Path(td)
        product_image(folder)
        boot_distribution()
        runtime_checks(folder, '--mutate' in sys.argv)
    return 0


if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_lan_module.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
