"""PowerMac link boundary, with fake MSR and resident tick registration."""
CHECK = dict(env=['ppc-cross'], name='ppc-link-platform-host', tier=2, budget=10, needs=[], covers=['sys/hal/time/tick_clock.h',
    'sys/platform/powermac/platform_link_inline.h', 'sys/net/nic.c', 'sys/net/nic.h', 'sys/net/link.c', 'sys/net/link_rx.inc', 'sys/net/link_host_api.inc', 'sys/net/link_selftest.inc',
    'sys/platform/powermac/link_platform.c', 'sys/hal/net/link_platform.h', 'sys/net/link.h',
    'sys/include/types.h', 'sys/include/callconv.h', 'sys/include/kernel_tick.h',
    'sys/include/net_eth.h', 'sys/kern/idle_work.h', 'sys/kern/irq/tick.h',
    'sys/arch/ppc/arch_io.h', 'sys/arch/ppc/ppc_frame.h', 'sys/arch/ppc/ppc_oea.h',
    'tools/tests/ppc_link_platform_host.c', 'tools/tests/test_ppc_link_platform.py',
    'sys/arch/ppc/ppc_boot.h',
    'sys/hal/time/clock_source.h',
    'sys/include/io.h',
    'sys/include/kprintf.h',
    'sys/include/platform_irq.h',
    'sys/include/remote_control.h',
    'sys/kern/irq/irq.h',
    'sys/kern/irq/irq_math.h',
    'sys/kern/irq/tick_budget.h',
    'sys/kern/kstring.h',
    'lib/le_internal.h',
    'sys/net/host_mailbox.h',
    'sys/net/link_notify.h',
    'sys/net/nic_rx.h',
    'sys/net/rx_work.h',
    'sys/platform/powermac/platform_io.h',
    'sys/platform/powermac/platform_irq_limits.h',
    'sdk/include/os32/os32_generations.h',
    'sdk/include/os32/os32_gfx2.h',
    'sdk/include/os32/os32_host_notice.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_ls.h',
    'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_ver_info.h',
    'tools/tests/host32.py'],
    basis=['AC-SYNC-1', 'AC-SYNC-2', 'AC-NET-3', 'TICK_ARCH_DESIGN §2'],
)
import os
from pathlib import Path
import subprocess
import tempfile
import host32


def run(ctx):
    original = (ctx.root / 'sys/platform/powermac/link_platform.c').read_text()
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
                ('idle_running = 1;', 'idle_running = 0;'),
                ('    idle_wait_step();', '    /* omitted work */'),
                ('if (!_irq_enabled()) {', 'if (0) {')]:
                assert original.count(anchor) == 1
                exercise(original.replace(anchor, replacement), False)
                print('RED runtime: ' + anchor, flush=True)
        ppc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
        cc = ppc / 'bin/powerpc-elf-gcc'
        if not cc.is_file():
            print('SKIP PowerPC object: toolchain unavailable', flush=True)
            return 1 if os.environ.get('OS32_REQUIRE_PPC') == '1' else 0
        subprocess.run([str(cc), '-std=gnu11',
            '-Wall', '-Wextra', '-Werror', '-ffreestanding', *includes,
            '-c', str(ctx.root / 'sys/platform/powermac/link_platform.c'),
            '-o', str(work / 'ppc.o')], check=True)
        for rel in ['sys/net/link.c', 'sys/net/nic.c']:
            subprocess.run([str(cc), '-std=gnu11', '-O2', '-Wall', '-Wextra',
                '-Werror', '-ffreestanding', '-D__cdecl=', *includes,
                *['-I' + str(ctx.root / p) for p in ['platform/powermac',
                  'net', 'lib', 'kernel/irq', 'sdk/include/os32']],
                '-c', str(ctx.root / rel), '-o', str(work / (Path(rel).stem + '.o'))],
                check=True)
        print('GREEN PowerPC platform/link/NIC standalone objects', flush=True)
    return 0
