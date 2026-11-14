"""PowerMac link boundary, with fake MSR and resident tick registration."""
CHECK = dict(name='ppc-link-platform-host', tier=2, budget=10, needs=[], covers=[
    'platform/powermac/link_platform.c', 'net/link_platform.h', 'net/link.h',
    'include/types.h', 'include/callconv.h', 'include/kernel_tick.h',
    'include/net_eth.h', 'kernel/idle_work.h', 'kernel/irq/tick.h',
    'arch/ppc/arch_io.h', 'arch/ppc/ppc_frame.h', 'arch/ppc/ppc_oea.h',
    'tools/tests/ppc_link_platform_host.c', 'tools/tests/test_ppc_link_platform.py',
    'tools/tests/host32.py'])
import os
from pathlib import Path
import subprocess
import tempfile
import host32


def run(ctx):
    original = (ctx.root / 'platform/powermac/link_platform.c').read_text()
    with tempfile.TemporaryDirectory(prefix='ppc-link-') as td:
        work = Path(td)
        source = work / 'link_platform.c'
        exe = work / 'probe'
        includes = ['-I' + str(ctx.root / p) for p in
                    ['.', 'include', 'arch/ppc']]
        def exercise(text, expected):
            source.write_text(text)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra',
                '-Werror', '-fno-pie', '-nostdlib', '-static', '-no-pie',
                *includes, '-DLINK_PLATFORM_SOURCE="' + str(source) + '"',
                str(ctx.root / 'tools/tests/ppc_link_platform_host.c'),
                '-o', str(exe)], check=True)
            rc = host32.run([str(exe)], runner='qemu').returncode
            assert (rc == 0) == expected, rc
            assert rc >= 0, rc
        exercise(original, True)
        print('GREEN now, nested EE, idle reentry, registration/retry', flush=True)
        if ctx.mutate:
            for anchor, replacement in [
                ('ppc_irq_restore(saved);', '(void)saved;'),
                ('idle_running = 1;', 'idle_running = 0;')]:
                assert original.count(anchor) == 1
                exercise(original.replace(anchor, replacement), False)
                print('RED runtime: ' + anchor, flush=True)
        ppc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
        cc = ppc / 'bin/powerpc-elf-gcc'
        if not cc.is_file():
            print('SKIP PowerPC object: toolchain unavailable', flush=True)
            return 0
        subprocess.run([str(cc), '-std=gnu11',
            '-Wall', '-Wextra', '-Werror', '-ffreestanding', *includes,
            '-c', str(ctx.root / 'platform/powermac/link_platform.c'),
            '-o', str(work / 'ppc.o')], check=True)
        print('GREEN PowerPC standalone object', flush=True)
    return 0
