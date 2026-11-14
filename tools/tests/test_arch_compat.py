#!/usr/bin/env python3
"""Shared compatibility vectors; partial host evidence never closes an ID."""
CHECK = dict(name='arch-compat-host', tier=2, budget=15, needs=[], covers=[
    'tools/tests/test_arch_compat.py', 'tools/tests/arch_compat_vectors.c',
    'tools/tests/host32.py', 'docs/decisions/ARCH_COMPAT_CONTRACT.md',
    'platform/pc98/link_platform.c', 'platform/powermac/link_platform.c',
    'net/link_platform.h', 'include/types.h', 'include/kernel_tick.h',
    'kernel/irq/tick.h', 'platform/pc98/tick_order.h',
    'kernel/idle_work.h', 'arch/ppc/arch_io.h'])
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


def exercise(root, work, vectors):
    (work / 'io.h').write_text('u32 irq_save(void);\n'
        'void irq_restore(u32); void _idle(void);\n')
    (work / 'arch_io.h').write_text('u32 ppc_irq_save(void);\n'
        'void ppc_irq_restore(u32); void _enable(void); void _halt(void);\n')
    (work / 'idt.h').write_text('#include "types.h"\nextern volatile u32 tick_count;\n')
    src = work / 'vectors.c'
    src.write_text(vectors)
    for provider in ('pc98', 'powermac'):
        exe = work / provider
        source = root / 'platform' / provider / 'link_platform.c'
        subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
            '-Werror', '-ffreestanding', '-nostdlib', '-static', '-fno-pie',
            '-no-pie', '-fno-stack-protector', '-I' + str(work), '-I' + str(root),
            '-I' + str(root / 'include'), '-DPROVIDER_SOURCE="' + str(source) + '"',
            str(src), '-o', str(exe)], check=True)
        rc = host32.run([str(exe)], runner='qemu').returncode
        if rc != 0:
            print('FAIL AC-TIME-3/host-counter ' + provider, flush=True)
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
        for name in IDS:
            print('UNIMPL ' + name, flush=True)
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
            print('PASS PowerPC provider cross compile', flush=True)
        if ctx.mutate:
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
