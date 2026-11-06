#!/usr/bin/env python3
"""3b core transaction: real resource construction over libc-free kernel seams.

No register/LFB access: common trident_device_fake supplies cold PCI values.
BAR geometry oracle: TRIDENT_S3_DESIGN §3, BARLEN_DESIGN §2 and Intel 8255x
SDM rev1.3 §4.1.10. Failure points are API-level, never product-only hooks.
"""
CHECK = dict(name='trident-resources', tier=1, needs=[], budget=5,
    covers=[
        'build/config.mk',
        'docs/decisions/realhw/BARLEN_DESIGN.md',
        'docs/decisions/realhw/TRIDENT_S3_DESIGN.md',
        'drivers/pci/pci.h',
        'drivers/pci/pci_decode.h',
        'drivers/video/trident/resources.c',
        'drivers/video/trident/trident.h',
        'exec/surface_limits.h',
        'gfx/gfx.h',
        'gfx/palette.h',
        'hwdev/gf/accel.h',
        'hwdev/gf/command.h',
        'hwdev/gf/display/api.h',
        'hwdev/gf/display/data.h',
        'hwdev/gf/feature5.h',
        'hwdev/gf/foundation.h',
        'hwdev/gf/internal.h',
        'hwdev/gf/production.h',
        'hwdev/gf/scripts.h',
        'hwdev/gf/vram_layout.h',
        'hwdev/gf/vram_view.h',
        'hwdev/gf_load_state.h',
        'hwdev/gfinit_mode.h',
        'include/appmem_types.h',
        'include/gfx_format.h',
        'include/gfx_hal.h',
        'include/gfx_mode.h',
        'include/kprintf.h',
        'include/memmap.h',
        'include/pc98.h',
        'include/pci_dev_contract.h',
        'include/pegc.h',
        'include/sys.h',
        'include/types.h',
        'include/video_service.h',
        'kdebug/gfinit_decode.h',
        'kdebug/gfinit_pll.h',
        'kdebug/gfinit_wire.h',
        'kdebug/recover.h',
        'kdebug/recover_gf.h',
        'lib/kstring.h',
        'mm/paging.h',
        'mm/pgalloc.h',
        'mm/physmem.h',
        'sdk/include/os32/os32_generations.h',
        'sdk/include/os32/os32_kapi_generated.h',
        'sdk/include/os32/os32_kapi_shared.h',
        'sdk/include/os32/os32_ls.h',
        'sdk/include/os32/os32_surface.h',
        'sdk/include/os32/os32_ver_info.h',
        'tools/tests/host32.py',
        'tools/tests/kernel_includes.py',
        'tools/tests/trident_device_fake.c',
        'tools/tests/trident_device_fake.h',
        'tools/tests/trident_resources_host.c',
    ])
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'drivers/video/trident/resources.c'
HOST = 'tools/tests/trident_resources_host.c'
FAKE = 'tools/tests/trident_device_fake.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector']
# name, product file, unique old text, replacement, runtime RED case.
MUTATIONS = [
    ('master', PRODUCT, 'paging_current_cr3() == paging_kernel_pd_phys()', '1', 1),
    ('live-as', PRODUCT, '!paging_live_addrspaces()', '1', 2),
    ('memory-enable', PRODUCT, '!(p->command & PCI_CMD_MEM_ENABLE)', '0', 4),
    ('revision-bound', PRODUCT, 'p->revision != TRI_RA266_REVISION', '0', 5),
    ('bar-type', PRODUCT, 'p->bar0 != MEM_TRIDENT_LFB_VA', '(p->bar0 & ~1u) != MEM_TRIDENT_LFB_VA', 6),
    ('full-overlap', PRODUCT, 'first < other_end && other_first < end', 'first == other_first && end == other_end', 8),
    ('unknown-not-veto', PRODUCT, 'if (end == first) end = first + 1;', 'if (end == first) return TRI_E_RESOURCE;', 9),
    ('ram-overlap', PRODUCT, 'if (pgalloc_range_has_ram(bases[i] / PAGE_SIZE, (bases[i] + decodes[i]) / PAGE_SIZE))',
        'if (0 && pgalloc_range_has_ram(bases[i] / PAGE_SIZE, (bases[i] + decodes[i]) / PAGE_SIZE))', 10),
    ('reserve-first', PRODUCT, 'if (!ledger_reserve_set(resources.owner, spans, TRI_BAR_COUNT))',
        'if (0 && !ledger_reserve_set(resources.owner, spans, TRI_BAR_COUNT))', 0),
    ('map-rc', PRODUCT, 'if (paging_map_phys(bases[i], bases[i], maps[i] / PAGE_SIZE, PAGE_RW | PTE_PCD))',
        'if (paging_map_phys(bases[i], bases[i], maps[i] / PAGE_SIZE, PAGE_RW | PTE_PCD) == 1)', 15),
    ('supervisor', PRODUCT, 'maps[i] / PAGE_SIZE, PAGE_RW | PTE_PCD)',
        'maps[i] / PAGE_SIZE, PAGE_RW | PTE_PCD | PTE_USER)', 0),
    ('map-rollback', PRODUCT, 'mapped[i] = true;', 'mapped[i] = false;', 16),
    ('map-record', PRODUCT, 'if (!ledger_resource_set_map(rids[i], bases[i] / PAGE_SIZE,',
        'if (0 && !ledger_resource_set_map(rids[i], bases[i] / PAGE_SIZE,', 17),
    ('surface-rollback', PRODUCT, 'if (!surface_created[i]) continue;', 'if (i == 0 || !surface_created[i]) continue;', 21),
    ('client-free', PRODUCT, 'if (client_allocated) {', 'if (false && client_allocated) {', 20),
    ('armed-busy', PRODUCT, 'if (armed) return TRI_E_BUSY;', 'if (false && armed) return TRI_E_BUSY;', 22),
    ('lease-busy', PRODUCT, 'surface_created[i] && ledger_surfaces[sids[i]].lease_count',
        'false && surface_created[i] && ledger_surfaces[sids[i]].lease_count', 23),
    ('display-private', PRODUCT, '.perm_max = LEDGER_PERM_NONE', '.perm_max = LEDGER_PERM_RW', 0),
    ('publisher-private', PRODUCT, 'gfx_surface_unready |= 1u << resources.display_sid;',
        'gfx_surface_unready |= 0u << resources.display_sid;', 0),
    ('ready-commit', PRODUCT, 'resources.ready = true;', 'resources.ready = false;', 0),
    ('no-reserve-repeat', PRODUCT, 'if (attempted) return boot_rc;', 'if (false && attempted) return boot_rc;', 0),
    ('client-clear', PRODUCT, 'kmemset(resources.client, 0, TRI_DISPLAY_BYTES);',
        'kmemset(resources.client, 0, 0);', 0),
    ('bridge-prefetch', PRODUCT, 'rc = bridge_window(dev, fn, PCI_CFG_PREF_WINDOW);',
        'rc = 0;', 45),
    ('multifunction', PRODUCT, 'header & PCI_HDR_MULTIFUNCTION ? PCI_FN_COUNT : 1',
        'header & PCI_HDR_MULTIFUNCTION ? 1 : 1', 35),
]


def build(folder, source):
    exe = folder / 'resources-host'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT),
                    '-I' + str(ROOT / 'drivers/video/trident'), *include_flags(ROOT),
                    '-nostdlib', '-static', '-no-pie', ROOT / HOST, ROOT / FAKE, source,
                    '-o', exe], check=True, capture_output=True)
    return exe


def execute(exe, mode, runner):
    return host32.run([str(exe)], input=struct.pack('<I', mode), runner=runner,
                      capture_output=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    with tempfile.TemporaryDirectory(prefix='trident-resources-') as td:
        folder = Path(td)
        # Compile the exact production translation unit using the real cross
        # toolchain, with no host define/header override. Wiring into the core
        # link remains the assigned 3c/3d work.
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-ffreestanding', '-I' + str(ROOT), *include_flags(ROOT),
                        '-c', ROOT / PRODUCT, '-o', folder / 'production.o'],
                       check=True, capture_output=True)
        exe = build(folder, ROOT / PRODUCT)
        for runner in runners:
            for mode in range(55):
                result = execute(exe, mode, runner)
                assert result.returncode == 0, (mode, result.returncode, result.stdout, result.stderr)
            print(f'GREEN {runner}: 55 boot/rollback cases, real resource construction, cold common fake', flush=True)
        if args.mutate:
            original = (ROOT / PRODUCT).read_text()
            for name, path, old, new, mode in MUTATIONS:
                assert path == PRODUCT and original.count(old) == 1, (name, old)
                mutant = folder / f'{name}.c'
                mutant.write_text(original.replace(old, new))
                exe = build(folder, mutant)  # compile failure is never runtime RED
                for runner in runners:
                    result = execute(exe, mode, runner)
                    assert result.returncode == 1 and b'FAIL ' in result.stdout, (
                        name, mode, result.returncode, result.stdout, result.stderr)
                print(f'RED runtime {name} case={mode}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_resources.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env,
                           close_fds=False)
