#!/usr/bin/env python3
"""ILP32 common mm with PowerMac provider; outside make check."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32
from paging_source import paging_source
from kernel_includes import include_flags
CHECK = dict(name='ppc-kboot-memory', tier=2, budget=60, covers=[
    'platform/powermac/memory.c', 'platform/powermac/memory.h',
    'platform/powermac/platform_memory_desc.h', 'platform/powermac/boot_capture.c',
    'tools/ppc/kboot_memory.py', 'tools/ppc/kboot_memory_host.c',
    'mm/*.c', 'mm/*.inc', 'mm/*.h', 'kernel/sys.c', 'include/*.h',
    'platform/powermac/*.h', 'tools/tests/host_arch/*.h',
    'tools/tests/host32.py', 'tools/tests/paging_source.py',
    'tools/tests/kernel_includes.py'])
# Product mutations, expected runtime exit, diagnostic, and reached marker.
MUTATIONS = [
    ('geometry-16', 'platform/powermac/platform_memory_desc.h',
     '#define PM_MEMORY_LEGACY_END 0x00f00000UL',
     '#define PM_MEMORY_LEGACY_END 0x01000000UL', 32, 'FAIL:INIT', 'REACHED:INIT'),
    ('system-map', 'mm/paging.c',
     '!(phys >= platform_memory_geometry.system_first &&',
     '!(0 && phys >= platform_memory_geometry.system_first &&',
     31, 'FAIL:SYSTEM-PTE', 'REACHED:PAGING'),
    ('low-device-shift', 'platform/powermac/platform_memory_desc.h',
     '{PM_MEMORY_COMPAT_FIRST, PM_MEMORY_COMPAT_END}\n};',
     '{PM_MEMORY_COMPAT_FIRST - 0x1000, PM_MEMORY_COMPAT_END}\n};',
     32, 'FAIL:INIT', 'REACHED:INIT'),
    ('hpt-allocator', 'mm/physmem.c',
     'if (end > low) overlay(m, low, end, PHYSMEM_RAM, PHYSMEM_SOURCE_LEGACY);',
     'if (end > low) overlay(m, low, end, PHYSMEM_RAM, PHYSMEM_SOURCE_LEGACY);\n'
     '    overlay(m, 0x1000, 0x1020, PHYSMEM_RAM, PHYSMEM_SOURCE_LEGACY);',
     34, 'FAIL:HPT-RAM', 'REACHED:INIT'),
]


def exercise(directory, mutation=None):
    for sub in ('include', 'kernel', 'mm', 'platform/powermac'):
        (directory / sub).mkdir(parents=True, exist_ok=True)
    paths = ['mm/paging.c', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/memory_boot.c',
             'kernel/sys.c', 'include/platform_memory.h',
             'platform/powermac/platform_memory_desc.h',
             'platform/powermac/memory.c', 'platform/powermac/boot_capture.c']
    for rel in paths:
        source = paging_source(ROOT / rel)
        if mutation and rel == mutation[1]:
            if source.count(mutation[2]) != 1:
                raise RuntimeError(f'{mutation[0]}: nonunique anchor')
            source = source.replace(mutation[2], mutation[3])
        dest = directory / rel
        if rel in ('mm/paging.c', 'mm/pgalloc.c', 'kernel/sys.c', 'mm/memory_boot.c'):
            dest = directory / 'kernel' / (Path(rel).stem + '_host_source.c')
        dest.write_text(source)
    exe = directory / 'memory'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
           '-Werror', '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie',
           '-fno-stack-protector', '-nostdlib', '-static', '-ffunction-sections',
           '-fdata-sections', '-Wl,--gc-sections', '-D__cdecl=',
           '-I'+str(directory / 'kernel'), '-I'+str(directory / 'platform/powermac'),
           '-I'+str(ROOT / 'platform/powermac'), '-I'+str(ROOT / 'tools/tests/host_arch'),
           '-I'+str(ROOT / 'arch/ppc'), *include_flags(ROOT),
           str(ROOT / 'tools/ppc/kboot_memory_host.c'), str(directory / 'mm/physmem.c'),
           str(directory / 'platform/powermac/memory.c'),
           str(ROOT / 'platform/powermac/kboot_reserve.c'),
           str(directory / 'platform/powermac/boot_capture.c'), '-o', str(exe)]
    subprocess.run(cmd, check=True, timeout=60)
    return host32.run([str(exe)], capture_output=True, text=True, timeout=30)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='kboot-memory-') as temp:
        directory = Path(temp)
        result = exercise(directory)
        if result.returncode or 'GREEN:MEMORY' not in result.stdout:
            raise RuntimeError(f'baseline {result.returncode}: {result.stdout}')
        print(result.stdout, end='')
        if args.mutate:
            for mutation in MUTATIONS:
                result = exercise(directory, mutation)
                _, _, _, _, code, diagnostic, marker = mutation
                if result.returncode != code or diagnostic not in result.stdout or marker not in result.stdout:
                    raise RuntimeError(f'{mutation[0]}: unexpected runtime {result.returncode}: {result.stdout}')
                print(f'RED: {mutation[0]}: exit={code}, {marker}, {diagnostic}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
