#!/usr/bin/env python3
"""Compile the standalone link platform contract for host and PowerPC."""
CHECK = dict(name='link-platform-host', tier=2, budget=10, needs=[], covers=[
    'platform/pc98/link_platform_inline.h', 'platform/pc98/link_platform.c', 'net/link_platform.h', 'include/types.h', 'include/callconv.h',
    'tools/tests/test_link_platform.py', 'tools/tests/host32.py',
    'kernel/idle_work.h', 'arch/x86/kernel/idt.h', 'include/io.h'])
import os
import host32
from pathlib import Path
import subprocess
import tempfile


def run(ctx):
    header = (ctx.root / 'net/link_platform.h').read_text()
    # CI has no PPC toolchain (same rule as ppc-sc): host cc is required, PPC runs where present.
    ppc = str(Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
              / 'bin/powerpc-elf-gcc')
    compilers = ['cc'] + ([ppc] if Path(ppc).is_file() else [])
    if len(compilers) == 1:
        print('SKIP PowerPC half: toolchain unavailable: ' + ppc, flush=True)
    source = '#include "link_platform.h"\n'
    for name, signature in [('now', 'u32 (*)(void)'), ('lock', 'u32 (*)(void)'),
                            ('unlock', 'void (*)(u32)'), ('idle', 'void (*)(void)')]:
        source += ('_Static_assert(__builtin_types_compatible_p('
                   '__typeof__(&link_platform_' + name + '), ' + signature +
                   '), "' + name + '");\n')
    with tempfile.TemporaryDirectory(prefix='link-platform-') as td:
        work = Path(td)
        (work / 'probe.c').write_text(source)
        def compile_header(cc, text):
            (work / 'link_platform.h').write_text(text)
            try:
                return subprocess.run([cc, '-Wall', '-Wextra', '-Werror',
                    '-std=gnu11', '-I' + str(ctx.root / 'include'), '-c',
                    str(work / 'probe.c'), '-o', str(work / 'probe.o')],
                    capture_output=True, text=True)
            except FileNotFoundError:
                print('FAIL compiler missing: ' + cc, flush=True)
                return None
        # Compile the real PC-98 implementation against observable IRQ/wait seams.
        (work / 'kernel').mkdir()
        (work / 'idt.h').write_text('#include "types.h"\nextern volatile u32 tick_count;\n')
        (work / 'io.h').write_text('unsigned int irq_save(void);\n'
            'void irq_restore(unsigned int); void _idle(void);\n')
        (work / 'kernel/idle_work.h').write_text('void idle_work_run(void);\n')
        probe = r'''#include "net/link_platform.h"
#ifdef TEST_PC98_INLINE
#include "platform/pc98/link_platform_inline.h"
#endif
volatile u32 tick_count;
static u32 flags = 0x202, stage;
unsigned int irq_save(void) { u32 f = flags; flags = 0; return f; }
void irq_restore(unsigned int f) { flags = f; }
void idle_work_run(void) { stage = stage == 0 ? 1 : 99; }
void _idle(void) { stage = stage == 1 ? 2 : 99; }
void _start(void) {
    int rc = 0;
    tick_count = 0xffffffffu;
    if (link_platform_now() != tick_count) rc = 1;
    tick_count = 0;
    if (link_platform_now() != 0) rc = 1;
    u32 outer = link_platform_lock(), inner = link_platform_lock();
    if (outer != 0x202 || inner != 0 || flags != 0) rc = 1;
    link_platform_unlock(inner);
    if (flags != 0) rc = 1;
    link_platform_unlock(outer);
    if (flags != 0x202) rc = 1;
    link_platform_idle();
    if (stage != 2) rc = 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
'''
        (work / 'runtime.c').write_text(probe)
        products = [(ctx.root / 'platform/pc98/link_platform.c').read_text(),
                    (ctx.root / 'platform/pc98/link_platform_inline.h').read_text()]
        (work / 'platform/pc98').mkdir(parents=True)
        mutations = [
            ('return tick_count;', 'return 0;', 'clock'),
            ('return irq_save();', 'return 0;', 'lock'),
            ('irq_restore(saved);', '(void)saved;', 'unlock'),
            ('idle_work_run();', '', 'idle work'),
            ('    _idle();', '', 'idle wait')]
        for inline, product in enumerate(products):
            variants = [(product, 'control')] + ([(product.replace(a, b), label)
                        for a, b, label in mutations] if ctx.mutate else [])
            for a, _, _ in mutations:
                assert product.count(a) == 1
            for text, label in variants:
                (work / 'platform.c').write_text(products[0] if inline else text)
                (work / 'platform/pc98/link_platform_inline.h').write_text(
                    text if inline else products[1])
                exe = work / 'runtime'
                subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall',
                    '-Wextra', '-Werror', '-ffreestanding', '-nostdlib', '-static',
                    '-fno-pie', '-no-pie', '-fno-stack-protector',
                    *(['-DTEST_PC98_INLINE'] if inline else []),
                    '-I' + str(work), '-I' + str(ctx.root),
                    '-I' + str(ctx.root / 'include'), str(work / 'runtime.c'),
                    str(work / 'platform.c'), '-o', str(exe)], check=True)
                rc = host32.run([str(exe)]).returncode
                assert rc == (0 if label == 'control' else 1), (inline, label, rc)
                print(('GREEN' if label == 'control' else 'RED') +
                      ' runtime: ' + ('inline ' if inline else 'external ') + label)
        failed = False
        for cc in compilers:
            result = compile_header(cc, header)
            if result is None or result.returncode:
                if result is not None:
                    print(result.stderr, flush=True)
                failed = True
                continue
            print('GREEN standalone declarations: ' + cc, flush=True)
            if ctx.mutate:
                anchor = 'u32 link_platform_now(void);'
                assert header.count(anchor) == 1
                result = compile_header(cc, header.replace(anchor,
                                            'void link_platform_now(void);'))
                assert result is not None and result.returncode != 0
                assert 'static assertion failed' in result.stderr
                print('RED compile: now return type: ' + cc, flush=True)
        return int(failed)
