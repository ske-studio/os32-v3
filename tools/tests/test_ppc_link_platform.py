"""PowerMac link boundary, with fake MSR and resident tick registration."""
CHECK = dict(name='ppc-link-platform-host', tier=2, budget=10, needs=[], covers=[
    'platform/powermac/link_platform_inline.h', 'net/nic.c', 'net/nic.h', 'net/link.c', 'net/link_rx.inc', 'net/link_host_api.inc', 'net/link_selftest.inc',
    'platform/powermac/link_platform.c', 'net/link_platform.h', 'net/link.h',
    'include/types.h', 'include/callconv.h', 'include/kernel_tick.h',
    'include/net_eth.h', 'kernel/idle_work.h', 'kernel/irq/tick.h',
    'arch/ppc/arch_io.h', 'arch/ppc/ppc_frame.h', 'arch/ppc/ppc_oea.h',
    'tools/tests/ppc_link_platform_host.c', 'tools/tests/test_ppc_link_platform.py',
    'arch/ppc/ppc_boot.h',
    'include/clock_source.h',
    'include/io.h',
    'include/kprintf.h',
    'include/platform_irq.h',
    'include/remote_control.h',
    'kernel/irq/irq.h',
    'kernel/irq/irq_math.h',
    'kernel/irq/tick_budget.h',
    'lib/kstring.h',
    'lib/le_internal.h',
    'net/host_mailbox.h',
    'net/link_notify.h',
    'net/nic_rx.h',
    'net/rx_work.h',
    'platform/powermac/platform_io.h',
    'platform/powermac/platform_irq_limits.h',
    'sdk/include/os32/os32_generations.h',
    'sdk/include/os32/os32_gfx2.h',
    'sdk/include/os32/os32_host_notice.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_ls.h',
    'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_ver_info.h',
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
                ('return tick_register(PM_TICK_LINK, fn);', 'return tick_register(0, fn);'),
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
        for rel in ['net/link.c', 'net/nic.c']:
            subprocess.run([str(cc), '-std=gnu11', '-O2', '-Wall', '-Wextra',
                '-Werror', '-ffreestanding', '-D__cdecl=', *includes,
                *['-I' + str(ctx.root / p) for p in ['platform/powermac',
                  'net', 'lib', 'kernel/irq', 'sdk/include/os32']],
                '-c', str(ctx.root / rel), '-o', str(work / (Path(rel).stem + '.o'))],
                check=True)
        print('GREEN PowerPC platform/link/NIC standalone objects', flush=True)
    return 0
