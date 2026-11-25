#!/usr/bin/env python3
"""3b core transaction: real resource construction over libc-free kernel seams.

No register/LFB access: common trident_device_fake supplies cold PCI values.
Resource contract: TRIDENT_FULL_DESIGN §2 and DESIGN.final.md F6 decisions. Failure points are API-level;
fixed BAR placement is required by the boot mapping contract.
"""
CHECK = dict(name='trident-resources', tier=1, needs=[], budget=16.1,
    covers=[
        'build/config.mk',
        'docs/decisions/realhw/BARLEN_DESIGN.md',
        'docs/decisions/realhw/TRIDENT_S3_DESIGN.md',
        'sys/dev/pci/pci.h',
        'sys/dev/pci/pci_decode.h',
        'sys/dev/video/trident/resources.c',
        'sys/dev/video/trident/trident.h',
        'sys/kern/exec/surface_limits.h',
        'sys/kern/exec/lease.h',
        'docs/decisions/realhw/TRIDENT_FULL_DESIGN.md',
        'sys/hal/gfx/gfx.h',
        'sys/hal/gfx/palette.h',
        'sys/dev/video/geforce/accel.h',
        'sys/dev/video/geforce/command.h',
        'sys/dev/video/geforce/display/api.h',
        'sys/dev/video/geforce/display/data.h',
        'sys/dev/video/geforce/feature5.h',
        'sys/dev/video/geforce/foundation.h',
        'sys/dev/video/geforce/internal.h',
        'sys/dev/video/geforce/production.h',
        'sys/dev/video/geforce/scripts.h',
        'sys/dev/video/geforce/vram_layout.h',
        'sys/dev/video/geforce/vram_view.h',
        'sys/dev/video/geforce/gf_load_state.h',
        'sys/dev/video/geforce/gfinit_mode.h',
        'sys/include/appmem_types.h',
        'sys/include/gfx_format.h',
        'sys/include/gfx_hal.h',
        'sys/include/gfx_mode.h',
        'sys/include/kprintf.h',
        'sys/include/memmap.h',
        'sys/platform/pc98/pc98.h',
        'sys/include/pci_dev_contract.h',
        'sys/platform/pc98/pegc.h',
        'sys/include/sys.h',
        'sys/include/types.h',
        'sys/include/video_service.h',
        'sys/dev/video/geforce/diag/gfinit_decode.h',
        'sys/dev/video/geforce/diag/gfinit_pll.h',
        'sys/dev/video/geforce/diag/gfinit_wire.h',
        'sys/dev/video/geforce/diag/recover.h',
        'sys/dev/video/geforce/diag/recover_gf.h',
        'sys/kern/kstring.h',
        'sys/kern/mm/paging.h',
        'sys/kern/mm/pgalloc.h',
        'sys/kern/mm/physmem.h',
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
    ],
    basis=['TRIDENT_FULL_DESIGN §2', 'TRIDENT_S3_DESIGN §4', 'BARLEN_DESIGN'],
)
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'sys/dev/video/trident/resources.c'
HOST = 'tools/tests/trident_resources_host.c'
FAKE = 'tools/tests/trident_device_fake.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector']
# name, product file, unique old text, replacement, runtime RED case.
MUTATIONS = [('master', 'sys/dev/video/trident/resources.c', 'paging_current_cr3() == paging_kernel_pd_phys()', '1', 1), ('live-as', 'sys/dev/video/trident/resources.c', '!paging_live_addrspaces()', '1', 2), ('memory-enable', 'sys/dev/video/trident/resources.c', '!(p->command & PCI_CMD_MEM_ENABLE)', '0', 4), ('ram-overlap', 'sys/dev/video/trident/resources.c', 'if (pgalloc_range_has_ram(bases[i] / PAGE_SIZE, (bases[i] + decodes[i]) / PAGE_SIZE))', 'if (0 && pgalloc_range_has_ram(bases[i] / PAGE_SIZE, (bases[i] + decodes[i]) / PAGE_SIZE))', 10), ('reserve-first', 'sys/dev/video/trident/resources.c', 'if (!ledger_reserve_set(resources.owner, spans, TRI_BAR_COUNT))', 'if (0 && !ledger_reserve_set(resources.owner, spans, TRI_BAR_COUNT))', 0), ('map-rc', 'sys/dev/video/trident/resources.c', 'if (paging_map_phys(vas[i], bases[i], maps[i] / PAGE_SIZE, PAGE_RW | PTE_PCD))', 'if (paging_map_phys(vas[i], bases[i], maps[i] / PAGE_SIZE, PAGE_RW | PTE_PCD) == 1)', 15), ('supervisor', 'sys/dev/video/trident/resources.c', 'maps[i] / PAGE_SIZE, PAGE_RW | PTE_PCD)', 'maps[i] / PAGE_SIZE, PAGE_RW | PTE_PCD | PTE_USER)', 0), ('map-rollback', 'sys/dev/video/trident/resources.c', 'mapped[i] = true;', 'mapped[i] = false;', 16), ('map-record', 'sys/dev/video/trident/resources.c', 'if (!ledger_resource_set_map(rids[i], bases[i] / PAGE_SIZE,', 'if (0 && !ledger_resource_set_map(rids[i], bases[i] / PAGE_SIZE,', 17), ('surface-rollback', 'sys/dev/video/trident/resources.c', 'if (!surface_created[i]) continue;', 'if (i == 0 || !surface_created[i]) continue;', 0), ('armed-busy', 'sys/dev/video/trident/resources.c', 'if (armed) return TRI_E_BUSY;', 'if (false && armed) return TRI_E_BUSY;', 22), ('lease-ceiling', 'sys/dev/video/trident/resources.c', '.perm_max = LEDGER_PERM_RW', '.perm_max = LEDGER_PERM_NONE', 0), ('publisher-private', 'sys/dev/video/trident/resources.c', 'gfx_surface_unready |= 1u << live_sids[0];', 'gfx_surface_unready |= 0u << live_sids[0];', 0), ('ready-commit', 'sys/dev/video/trident/resources.c', 'resources.ready = true;', 'resources.ready = false;', 0), ('no-reserve-repeat', 'sys/dev/video/trident/resources.c', 'if (attempted) return boot_rc;', 'if (false && attempted) return boot_rc;', 0)]

MUTATIONS += [
    ('final-required', PRODUCT, 'if (final) capacity_final = true;', 'if (false) capacity_final = true;', 75),
    ('resize-regen', PRODUCT, 'bool recreate = display_capacity && display_bytes != display_capacity;', 'bool recreate = false;', 75),
    ('same-sid', PRODUCT, 'if (sids_fixed && created[0] != fixed_sids[0]) goto cleanup;', 'if (false) goto cleanup;', 79),
    ('fixed-pages', PRODUCT, '.npages = display_bytes / PAGE_SIZE,', '.npages = pitch * mode->vd / PAGE_SIZE,', 75),

    ('client-regen', PRODUCT, 'if (!ledger_surface_regen(resources.client_sid, &sf)) goto done;', 'if (0) goto done;', 56),
    ('display-regen', PRODUCT, 'if (!ledger_surface_regen(resources.display_sid, &sf)) goto done;', 'if (0) goto done;', 56),
    ('capacity-fixed', PRODUCT, 'if (capacity_final && final && display_bytes != display_capacity) return TRI_E_STATE;', 'if (0) return TRI_E_STATE;', 58),
    ('client-alias', PRODUCT, '.backing = LEDGER_SB_VRAM,', '.backing = LEDGER_SB_MMIO,', 0),
    ('lease-revoke', PRODUCT, '            revoked += lease_revoke_surface(sids[i]);\n            if (ledger_surfaces[sids[i]].lease_count) revoked++;\n        }\n        if (revoked)', '            revoked += 0;\n            if (ledger_surfaces[sids[i]].lease_count) revoked++;\n        }\n        if (revoked)', 66),
    ('lease-revoke-rollback', PRODUCT, '            revoked += lease_revoke_surface(sids[i]);\n            if (ledger_surfaces[sids[i]].lease_count) revoked++;\n        }\n    }', '            revoked += 0;\n            if (ledger_surfaces[sids[i]].lease_count) revoked++;\n        }\n    }', 24),
    ('reload-caller', PRODUCT, 'paging_load_cr3(cr3);', '(void)cr3;', 56),
    ('bar0-fixed', PRODUCT, 'bases[0] != TRI_BAR0_PHYS', 'false', 55),
    ('bar1-fixed', PRODUCT, 'bases[1] != TRI_BAR1_PHYS', 'false', 32),
    ('bar-type', PRODUCT, '(p->bar0 & 0x07u) || (p->bar1 & 0x07u) || ', '', 6),
]

MUTATIONS += [
    ('ram-backing', PRODUCT, 'sf.owner = LEDGER_OWNER_BOOT;\n            sf.backing = LEDGER_SB_RAM;',
     'sf.owner = LEDGER_OWNER_BOOT;\n            sf.backing = LEDGER_SB_VRAM;', 0),
    ('ram-cache', PRODUCT, 'sf.cache = LEDGER_CACHE_WB;\n            if (!pgalloc_alloc',
     'sf.cache = LEDGER_CACHE_UC;\n            if (!pgalloc_alloc', 0),
    ('ram-free', PRODUCT, '(void)pgalloc_free_n_owner(sf.owner, sf.first, sf.npages);',
     '(void)sf.owner;', 21),
]


def build(folder, source, fallback=1):
    exe = folder / 'resources-host'
    host_source = folder / 'resources-seam.c'
    host_source.write_text(Path(source).read_text().replace('#include \"io.h\"',
        'unsigned int host_irq_save(void);\nvoid host_irq_restore(unsigned int);\n'
        '#define irq_save host_irq_save\n#define irq_restore host_irq_restore'))
    subprocess.run(['gcc', *FLAGS, f'-DTRI_RAM_FALLBACK_DEFAULT={fallback}', '-I' + str(ROOT),
                    '-I' + str(ROOT / 'drivers/video/trident'), *include_flags(ROOT),
                    '-nostdlib', '-static', '-no-pie', ROOT / HOST, ROOT / FAKE, host_source,
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
        for fallback in (0, 1):
            exe = build(folder, ROOT / PRODUCT, fallback)
            for runner in runners:
                for mode in range(83):
                    result = execute(exe, mode, runner)
                    assert result.returncode == 0, (fallback, mode, result.stdout, result.stderr)
            print(f'GREEN default={fallback}', flush=True)
        exe = build(folder, ROOT / PRODUCT)
        for runner in runners:
            for mode in range(83):
                result = execute(exe, mode, runner)
                assert result.returncode == 0, (mode, result.returncode, result.stdout, result.stderr)
            print(f'GREEN {runner}: 83 boot/configure/rollback cases, real resource construction, cold common fake', flush=True)
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
            exe = build(folder, ROOT / PRODUCT)
            for runner in runners:
                for mode in range(83):
                    result = execute(exe, mode, runner)
                    assert result.returncode == 0, (mode, result)
            print('RESTORED GREEN resources', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_resources.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env,
                           close_fds=False)
