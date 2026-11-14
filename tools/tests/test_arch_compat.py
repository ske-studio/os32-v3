#!/usr/bin/env python3
"""Shared compatibility vectors; partial host evidence never closes an ID."""
# Privileged MSR operations are fake; the real idle C body runs on host.
# Saved values remain opaque. Cross compilation independently checks PPC code.
CHECK = dict(name='arch-compat-host', tier=2, budget=15, needs=[], covers=[
    'tools/tests/test_arch_compat.py', 'tools/tests/arch_compat_vectors.c',
    'tools/tests/host32.py', 'docs/decisions/ARCH_COMPAT_CONTRACT.md',
    'platform/pc98/link_platform.c', 'platform/powermac/link_platform.c',
    'net/link_platform.h', 'include/types.h', 'include/kernel_tick.h',
    'kernel/irq/tick.h', 'platform/pc98/tick_order.h',
    'platform/powermac/mpic.c', 'platform/powermac/mpic.h',
    'platform/powermac/mpic_tree.h', 'arch/ppc/ppc_frame.h',
    'arch/ppc/ppc_trap.h', 'include/platform_irq.h',
    'platform/powermac/platform_irq_limits.h', 'kernel/irq/irq_math.h',
    'kernel/idle_work.h', 'arch/ppc/arch_io.h', 'arch/ppc/kernel_dec.c',
    'arch/ppc/ppc_oea.h',
    'arch/ppc/ppc_pd.h', 'kernel/time/tick_clock.h',
    'platform/powermac/kboot_info.h', 'kernel/irq/irq.h'])
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
IDS = ('AC-TIME-1/rational-wrap-context', 'AC-TIME-2/source-unavailable',
       'AC-TIME-3/jiffy-unit', 'AC-SYNC-1/nested-restore',
       'AC-SYNC-2/return-enabled-no-lost-wakeup', 'AC-IRQ-1/claim-spurious-complete')
# Independent inventory makes a missing registration a failing negative control.
REQUIRED = {'AC-TIME-1', 'AC-TIME-2', 'AC-TIME-3',
            'AC-SYNC-1', 'AC-SYNC-2', 'AC-IRQ-1'}


def exercise(root, work, vectors, mpic=None, idle=None, provider_text=None):
    product = (root / 'arch/ppc/kernel_dec.c').read_text()
    start = product.index('void arch_cpu_idle(void)')
    end = product.index('void arch_cpu_halt(void)', start)
    (work / 'idle.c').write_text(idle if idle is not None else product[start:end])
    (work / 'io.h').write_text('u32 irq_save(void);\n'
        'void irq_restore(u32); void _idle(void);\n')
    (work / 'arch_io.h').write_text('u32 ppc_irq_save(void);\n'
        'void ppc_irq_restore(u32); void _enable(void); void _halt(void); void _idle(void);\n')
    (work / 'idt.h').write_text('#include "types.h"\nextern volatile u32 tick_count;\n')
    src = work / 'vectors.c'
    src.write_text(vectors)
    for provider in ('powermac', 'pc98'):
        exe = work / provider
        source = root / 'platform' / provider / 'link_platform.c'
        if provider == 'powermac' and provider_text is not None:
            source = work / 'provider.c'
            source.write_text(provider_text)
        subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
            '-Werror', '-ffreestanding', '-nostdlib', '-static', '-fno-pie',
            '-no-pie', '-fno-stack-protector', '-I' + str(work), '-I' + str(root),
            '-I' + str(root / 'include'), '-DPROVIDER_SOURCE="' + str(source) + '"',
            '-DIDLE_SOURCE="' + str(work / 'idle.c') + '"',
            *(['-DPM_MPIC_HOST_TEST', '-DMPIC_SOURCE="' + str(mpic or root / 'platform/powermac/mpic.c') + '"',
               '-I' + str(root / 'arch/ppc'), '-I' + str(root / 'platform/powermac')]
              if provider == 'powermac' else []), str(src), '-o', str(exe)], check=True)
        try:
            rc = subprocess.run(['qemu-i386', str(exe)], timeout=2).returncode
        except subprocess.TimeoutExpired:
            print('RED runtime: idle did not return ' + provider, flush=True)
            return False
        if rc != 0:
            print('FAIL compatibility vectors ' + provider + ' rc=' + str(rc), flush=True)
            return False
        print('PASS AC-TIME-3/host-counter ' + provider, flush=True)
    return True


def inventory(ids):
    return {s.split('/')[0] for s in ids} == REQUIRED and len(ids) == len(REQUIRED)


def run(ctx):
    if not inventory(IDS):
        print('FAIL ID inventory', flush=True)
        return 1
    vectors = (ctx.root / 'tools/tests/arch_compat_vectors.c').read_text()
    with tempfile.TemporaryDirectory(prefix='arch-compat-') as td:
        work = Path(td)
        if not exercise(ctx.root, work, vectors):
            return 1
        print('PASS AC-SYNC-2/return-enabled-no-lost-wakeup powermac '
              '(host real idle C, fake EE; nop wait)', flush=True)
        for name in IDS:
            if not name.startswith('AC-SYNC-2/'):
                print(('PASS ' if name.startswith('AC-IRQ-1') else 'UNIMPL ') + name +
                      (' powermac (pc98 UNIMPL)' if name.startswith('AC-IRQ-1') else ''), flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        if not cc.is_file() or not os.access(cc, os.X_OK):
            print('SKIP PowerPC cross compile: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1':
                print('FAIL PowerPC toolchain required', flush=True)
                return 1
        else:
            subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-I' + str(ctx.root), '-I' + str(ctx.root / 'include'),
                '-I' + str(ctx.root / 'arch/ppc'), '-c',
                str(ctx.root / 'platform/powermac/link_platform.c'), '-o', str(work / 'ppc.o')], check=True)
            subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-I' + str(ctx.root), '-I' + str(ctx.root / 'include'),
                '-I' + str(ctx.root / 'arch/ppc'),
                '-I' + str(ctx.root / 'platform/powermac'), '-c',
                str(ctx.root / 'arch/ppc/kernel_dec.c'),
                '-o', str(work / 'dec.o')], check=True)
            print('PASS PowerPC provider + real idle cross compile', flush=True)
        if ctx.mutate:
            product = (ctx.root / 'platform/powermac/mpic.c').read_text()
            for label, old, new in (
                ('claim-zero', ')) return IRQ_ERR_INVAL;', ')) return 0;'),
                ('missing-finish', '*out = (struct irq_claim){token, token, 1, 1};',
                 'out->line = token; out->native_token = token; out->deliver = 1;'),
                ('double-eoi', 'active = 0;\n    write_reg(EOI, 0);',
                 'active = 0;\n    write_reg(EOI, 0); write_reg(EOI, 0);')):
                assert product.count(old) == 1
                mutant_path = work / 'mpic.c'
                mutant_path.write_text(product.replace(old, new).replace(
                    'struct irq_claim claim;', 'struct irq_claim claim = {0};'))
                assert not exercise(ctx.root, work, vectors, mutant_path)
                print('RED runtime: ' + label, flush=True)
            product = (ctx.root / 'arch/ppc/kernel_dec.c').read_text()
            a = product.index('void arch_cpu_idle(void)')
            b = product.index('void arch_cpu_halt(void)', a)
            idle = product[a:b]
            anchor_idle = '__asm__ volatile("nop" ::: "memory");'
            assert idle.count(anchor_idle) == 1
            for replacement in ('for (;;) __asm__ volatile("" ::: "memory");',
                                'irq_state = 0;'):
                assert not exercise(ctx.root, work, vectors,
                                    idle=idle.replace(anchor_idle, replacement))
                print('RED runtime: idle ' + replacement, flush=True)
            provider = (ctx.root / 'platform/powermac/link_platform.c').read_text()
            anchor_work = '    idle_work_run();'
            assert provider.count(anchor_work) == 1
            assert not exercise(ctx.root, work, vectors,
                                provider_text=provider.replace(anchor_work, ''))
            print('RED runtime: omitted idle work', flush=True)
            anchor = '0, 3, 0xfffffffdu, 0xffffffffu };'
            assert vectors.count(anchor) == 2
            before, after = vectors.rsplit(anchor, 1)
            mutant = before + '0, 4, 0xfffffffdu, 0xffffffffu };' + after
            assert not exercise(ctx.root, work, mutant)
            print('RED runtime: vector expectation', flush=True)
            assert not inventory(IDS[:-1])
            print('RED inventory: removed ID', flush=True)
    return 0


if __name__ == '__main__':
    from types import SimpleNamespace
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
