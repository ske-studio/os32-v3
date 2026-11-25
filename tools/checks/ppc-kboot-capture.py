"""HWOP_COMMON_DESIGN §2.3. Exercise the real OF capture before detach."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-kboot-capture', tier=2, budget=180, needs=[],
             basis=['docs/decisions/realhw/HWOP_COMMON_DESIGN.md §2.3'],
             covers=['tools/checks/ppc-kboot-capture.py', 'sys/platform/powermac/pci_hosts.inc',
                     'tools/ppc/pci_hosts_fake.c', 'sys/arch/ppc/kernel_boot.h', 'sys/kern/mm/paging.h',
                     'sys/platform/powermac/gmac_tree.c', 'sys/platform/powermac/lan_boot.h',
                     'sys/platform/powermac/pci_capture.inc', 'sys/platform/powermac/pci_capture.h',
                     'sys/platform/powermac/via_tree.c', 'sys/platform/powermac/via_window.h',
                     'sys/include/memmap.h', 'sys/platform/powermac/kboot_capture.c', 'sys/platform/powermac/kboot_info.h',
                     'tools/ppc/kboot_capture.py', 'tools/ppc/kboot_capture_fake.c',
                     'sys/platform/powermac/ofw.h', 'sys/platform/powermac/platform_of_layout.h',
                     'sys/arch/ppc/ppc_boot.h', 'sys/arch/ppc/ppc_pmap.h', 'sys/arch/ppc/ppc_oea.h',
                     'sys/include/types.h', 'sys/include/callconv.h', 'sys/kern/kstring.h',
                     'sys/kern/kstring_c.c', 'tools/tests/host32.py'])


def run(ctx):
    import host32
    with tempfile.TemporaryDirectory(prefix='pci-hosts-') as temp:
        exe = Path(temp) / 'test'
        subprocess.run(['gcc', '-m32', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-nostdinc', '-nostdlib', '-ffreestanding', '-static', '-fno-pie',
                        '-no-pie', '-D__cdecl=', '-Iplatform/powermac', '-Iarch/ppc', '-Iinclude', '-Imm',
                        'tools/ppc/pci_hosts_fake.c', '-o', str(exe)], check=True)
        host32.run([str(exe)], check=True)
    print('GREEN host IDs preserve isolated state and complete window pairs', flush=True)
    return subprocess.call(['python3', '-B', 'tools/ppc/kboot_capture.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
