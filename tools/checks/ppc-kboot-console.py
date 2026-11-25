"""ILP32 diagnostic formatter and console runtime checks."""
import os
from pathlib import Path
import shutil
import subprocess

CHECK = dict(name='ppc-kboot-console', budget=20, needs=[], env=['ppc-cross', 'qemu-ppc'],
    basis=['docs/design/lib/kprintf.md'],
    covers=['sys/platform/powermac/kboot_console.c', 'sys/platform/powermac/log_printf.c',
            'sys/platform/powermac/log.c', 'sys/platform/powermac/kboot_log.h',
            'sys/platform/powermac/escc.c', 'sys/platform/powermac/ofw.h',
            'sys/platform/powermac/platform_of_layout.h', 'sys/arch/ppc/ppc_boot.h',
            'sys/platform/powermac/platform_kprintf.h', 'sys/platform/pc98/platform_kprintf.h',
            'sys/include/console.h', 'sys/include/kprintf.h', 'sys/include/types.h', 'sys/include/callconv.h',
            'sys/kern/kstring.h', 'sys/kern/kstring_c.c', 'sys/kern/kprintf.c', 'sys/kern/kprintf_format.inc',
            'sdk/include/os32/*.h', 'tools/ppc/kboot_console.py',
            'tools/ppc/kboot_console_host.c', 'tools/ppc/kboot_format_host.c', 'build/arch/ppc.mk'])


def run(ctx):
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    required = [str(cross/'bin/powerpc-elf-gcc'), 'qemu-i386', 'qemu-ppc']
    missing = [tool for tool in required if not shutil.which(tool)]
    if missing:
        strict = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if strict else 'SKIP') + ' ppc-kboot-console: missing ' + ', '.join(missing))
        return int(strict)
    args = ['python3', '-B', 'tools/ppc/kboot_console.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root)
