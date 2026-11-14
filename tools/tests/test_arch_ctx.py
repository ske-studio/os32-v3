#!/usr/bin/env python3
"""W4b C evidence; no ID is closed by these partial host vectors."""
CHECK = dict(name='arch-ctx-host', tier=2, budget=15, needs=[], covers=[
    'tools/tests/test_arch_ctx.py', 'tools/tests/arch_ctx_host.c',
    'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk',
    'arch/ppc/kernel_trap.c', 'arch/ppc/*.h',
    'arch/x86/kernel/syscall_entry.c', 'arch/x86/arch_exec_storage.h',
    'exec/exec_syscall.inc', 'kernel/idle_work.[ch]', 'kernel/irq/irq.h',
    'init/ppc_kernel.c', 'platform/powermac/lan_monitor.c', 'platform/powermac/platform_irq_limits.h',
    'include/**', 'sdk/include/os32/**'])
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRE_W4C = False  # W4c integration changes this one switch to False.
VECTORS = {1: 'AC-CTX-4/FG1-FG0-IRQ-reentry', 2: 'AC-CTX-2/7/IRQ-depth',
           3: 'AC-CTX-1/7/entry-reject', 4: 'AC-CTX-1/5/syscall-C',
           5: 'AC-CTX-7/onfault-irq-save'}


def replace_once(source, old, new):
    assert source.count(old) == 1, old
    return source.replace(old, new, 1)


def exercise(root, work, provider, vector, trap, idle):
    (work / 'trap.c').write_text(trap)
    (work / 'idle.c').write_text(idle)
    exe = work / 'host'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
        '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
        '-nostdlib', '-static', '-no-pie', '-I' + str(work),
        '-I' + str(root / ('arch/ppc' if provider == 'ppc' else 'arch/x86')),
        *include_flags(root), '-DPPC_TRAP_HOST_TEST',
        *(['-DCTX_PPC'] if provider == 'ppc' else []),
        '-DCTX_VECTOR=' + str(vector), '-DCTX_IDLE_SOURCE="' + str(work / 'idle.c') + '"',
        '-DCTX_TRAP_SOURCE="' + str(work / 'trap.c') + '"',
        str(root / 'tools/tests/arch_ctx_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)], runner='qemu').returncode


def run(ctx):
    root = ctx.root
    ppc = (root / 'arch/ppc/kernel_trap.c').read_text()
    x86 = (root / 'arch/x86/kernel/syscall_entry.c').read_text()
    idle = (root / 'kernel/idle_work.c').read_text()
    common = (root / 'exec/exec_syscall.inc').read_text()
    monitor = (root / 'platform/powermac/lan_monitor.c').read_text()
    with tempfile.TemporaryDirectory(prefix='arch-ctx-') as td:
        work = Path(td)
        (work / 'io.h').write_text('int _irq_enabled(void);\n')
        # Execute the actual entry work and actual guarded onfault owner.
        start = common.index('    ring3_gui_pump();')
        (work / 'ctx_entry.inc').write_text(common[start:common.index('    arch_kapi_source(', start)])
        start = monitor.index('int pm_lan_copy8(')
        (work / 'ctx_onfault.inc').write_text(monitor[start:monitor.index('\nvoid pm_lan_execute(', start)])
        probes = (root / 'init/ppc_kernel.c').read_text()
        start = probes.index('static int probe_dsi(')
        (work / 'ctx_probes.inc').write_text(probes[start:probes.index('/* Remap one VA', start)])
        for provider, trap, vectors in [('ppc', ppc, VECTORS), ('x86', x86, (1, 4))]:
            for vector in vectors:
                rc = exercise(root, work, provider, vector, trap, idle)
                expected = 42 if PRE_W4C and provider == 'ppc' and vector == 4 else 0
                if rc != expected:
                    print(f'FAIL {VECTORS[vector]} {provider}: rc={rc}, expected={expected}', flush=True)
                    return 1
                status = 'W4c 前は期待の RED' if expected == 42 else 'PASS'
                print(f'{status} {VECTORS[vector]} {provider}', flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        if not cc.is_file() or not os.access(cc, os.X_OK):
            print('SKIP PowerPC cross compile: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1':
                print('FAIL PowerPC toolchain required', flush=True)
                return 1
        else:
            subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-I' + str(root), '-I' + str(root / 'arch/ppc'),
                '-I' + str(root / 'include'), '-I' + str(root / 'platform/powermac'),
                '-c', str(root / 'arch/ppc/kernel_trap.c'),
                '-o', str(work / 'ppc.o')], check=True)
            print('PASS PowerPC real trap cross compile', flush=True)
        if ctx.mutate:
            # Before W4c, positive control is a labelled prospective C boundary.
            # Never pass this synthetic source off as evidence for the real PPC.
            enabled = ppc
            if PRE_W4C:
                enabled = replace_once(enabled, '            exec_syscall_dispatch(&call);',
                    '            ppc_enable();\n            exec_syscall_dispatch(&call);\n'
                    '            (void)ppc_irq_save();')
            assert exercise(root, work, 'ppc', 4, enabled, idle) == 0
            print('PASS syscall enable mutation control' +
                  (' (synthetic W4c boundary)' if PRE_W4C else ''), flush=True)
            mutant = replace_once(enabled, '            ppc_enable();', '')
            assert exercise(root, work, 'ppc', 4, mutant, idle) == 42
            print('RED runtime: removed syscall enable', flush=True)
            mutant = replace_once(enabled, '            (void)ppc_irq_save();', '')
            rc = exercise(root, work, 'ppc', 4, mutant, idle)
            assert rc != 0, rc
            print(f'RED runtime: removed syscall C exit disable vector=4 rc={rc}', flush=True)
            probe_source = (work / 'ctx_probes.inc').read_text()
            for function in ('probe_dsi', 'probe_isi'):
                old = 'static int ' + function + '(u32 va)\n{\n    u32 flags = ppc_irq_save();'
                mutant = replace_once(probe_source, old, old.replace('ppc_irq_save()', 'msr'))
                (work / 'ctx_probes.inc').write_text(mutant)
                assert exercise(root, work, 'ppc', 5, ppc, idle) == 1
                print('RED runtime: onfault mask ' + function, flush=True)
            (work / 'ctx_probes.inc').write_text(probe_source)
            for anchor in ('            ++kctx_irq_depth;', '            --kctx_irq_depth;'):
                mutant = replace_once(ppc, anchor, '')
                assert exercise(root, work, 'ppc', 2, mutant, idle) == 1
                print('RED runtime: IRQ dispatch depth ' + anchor.strip(), flush=True)
            for anchor, label in [
                ('    if (!_irq_enabled()) return;', 'idle state'),
                ('    if (kctx_irq_depth != 0) return;', 'idle depth'),
                ('    if (running) return;', 'idle running')]:
                mutant = replace_once(idle, anchor, '')
                assert exercise(root, work, 'ppc', 1, ppc, mutant) == 1
                print('RED runtime: ' + label, flush=True)
        for contract in (1, 2, 4, 5, 7):
            print(f'UNIMPL AC-CTX-{contract}/full acceptance requires W4e guest evidence', flush=True)
    return 0


if __name__ == '__main__':
    from types import SimpleNamespace
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
