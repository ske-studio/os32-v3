"""H4/H5: real production resource lookup, adapter and paging/ledger lifetime."""
CHECK = dict(name='gf1-instmem-map-host', tier=1,
             covers=['tools/tests/gf_guard_fake.h', 'hwdev/gf/instmem_map.c',
                     'hwdev/gf/*.h', 'include/gf_production.h', 'include/memmap.h',
                     'drivers/video/geforce/geforce_platform.c',
                     'drivers/video/geforce/geforce_module.h',
                     'mm/paging.c', 'mm/paging.h', 'mm/paging_*.inc',
                     'mm/pgalloc.c', 'mm/pgalloc.h', 'mm/pgalloc_*.inc',
                     'mm/physmem.c', 'mm/physmem.h',
                     'tools/tests/gf1_instmem_map_host.c', 'tools/tests/test_gf1_instmem_map.py',
                     'tools/tests/pgalloc_host_fixture.h', 'tools/tests/paging_source.py',
                     'tools/tests/host_arch/arch_cpu.h', 'tools/tests/host_arch/arch_io.h',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk'],
             needs=[], budget=30.0)
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
from paging_source import paging_source

ROOT = Path(__file__).resolve().parents[2]
ADAPTER = 'hwdev/gf/instmem_map.c'
PLATFORM = 'drivers/video/geforce/geforce_platform.c'
PAGING = 'mm/paging_map.inc'
# Each anchor is unique in its product input. Builds must succeed and every
# mutant must report an assertion failure at runtime, followed by a GREEN control.
MUTATIONS = [
    ('failed-unmap-registration', PAGING,
     '((p << PAGE_SHIFT) | PAGE_RW | PTE_PCD)) return -1;',
     '((p << PAGE_SHIFT) | PAGE_RW | PTE_PCD)) { ledger_device_map_commit(*slot, 0); return -1; }'),
    ('map-end-pfn', ADAPTER,
     'paging_device_map(resource_owner, rid, first, first + bytes / PAGE_SIZE)',
     'paging_device_map(resource_owner, rid, first, first + bytes / PAGE_SIZE - 1)'),
    ('unmap-end-pfn', ADAPTER,
     'paging_device_unmap(resource_owner, rid, first, first + bytes / PAGE_SIZE)',
     'paging_device_unmap(resource_owner, rid, first, first + bytes / PAGE_SIZE - 1)'),
    ('lookup-rid', PLATFORM, '*rid = boot_resource_ids[match];', '*rid = match;'),
    ('lookup-unpublished', PLATFORM, '!boot_resources_valid || !owner', 'false || !owner'),
    ('failed-unmap-ownership', ADAPTER,
     'f->map_ops->unmap(f->map_owner, f->bar2, f->imem.aperture_bytes))',
     '(f->map_ops->unmap(f->map_owner, f->bar2, f->imem.aperture_bytes), 0))'),
]


def relocate(text, directory):
    """Keep quoted includes relative to their original product directory."""
    return re.sub(r'^#include "([^"]+)"',
                  lambda m: ('#include "' + str((directory / m[1]).resolve()) + '"'
                             if (directory / m[1]).is_file() else m[0]),
                  text, flags=re.M)


def main():
    originals = {name: (ROOT / name).read_text() for name in (ADAPTER, PLATFORM, PAGING)}
    with tempfile.TemporaryDirectory(prefix='gf1-map-') as td:
        tmp = Path(td)
        exe = tmp / 'host'
        paging = paging_source(ROOT / 'mm/paging.c')
        (tmp / 'pgalloc_source.c').write_text(paging_source(ROOT / 'mm/pgalloc.c'))
        command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                   '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                   '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
                   '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                   '-DPHYSMEM_HOST_TEST=1', '-I' + str(tmp),
                   '-I' + str(ROOT / 'tools/tests/host_arch'),
                   '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT),
                   str(ROOT / 'tools/tests/gf1_instmem_map_host.c'),
                   str(tmp / 'instmem_map.c'), str(ROOT / 'mm/physmem.c'),
                   '-o', str(exe)]

        def build(inputs):
            (tmp / 'paging_source.c').write_text(paging.replace(originals[PAGING], inputs[PAGING]))
            for name, output in ((ADAPTER, 'instmem_map.c'), (PLATFORM, 'geforce_platform_source.c')):
                (tmp / output).write_text(relocate(inputs[name], (ROOT / name).parent))
            subprocess.run(command, check=True)

        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            build(originals)
            host32.run([str(exe)], runner=runner, check=True)
            print(f'GREEN runner={runner}', flush=True)
            if '--mutate' in sys.argv:
                for label, product, old, new in MUTATIONS:
                    assert originals[product].count(old) == 1, label
                    inputs = dict(originals)
                    inputs[product] = inputs[product].replace(old, new, 1)
                    build(inputs)
                    result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
                    assert result.returncode == 1 and 'FAIL ' in result.stdout, (
                        label, result.returncode, result.stdout, result.stderr)
                    print(f'MUTATE runtime RED {label}: {result.stdout.strip()}', flush=True)
                    build(originals)
                    host32.run([str(exe)], runner=runner, check=True)
                    print(f'RESTORED GREEN {label}', flush=True)


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf1_instmem_map.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
