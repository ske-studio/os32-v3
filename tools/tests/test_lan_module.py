"""T5c-4 real resident LAN adapter/runtime under ILP32; device calls are seams.

LB's real driver/PCI and SerialFS transport regressions remain separate checks.
This suite establishes the owner lifetime across those callbacks, not NIC I/O.
"""
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
SOURCE = ROOT / 'kernel/lan_module.c'
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
    imports = (ROOT / 'kernel/kdebug_imports.def').read_text()
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
    kernel = (ROOT / 'kernel/kernel.c').read_text()
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
                        ROOT / 'kernel/module_runtime.c', ROOT / 'kernel/module_image.c',
                        ROOT / 'platform/pc98/lan_match.c',
                        ROOT / 'fs/vfs.c', ROOT / 'drivers/dev.c', '-o', exe], check=True)

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
