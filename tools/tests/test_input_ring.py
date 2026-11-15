#!/usr/bin/env python3
"""Internal input ring and clock contract."""
CHECK = dict(name='input-ring-host', tier=1, budget=5, needs=[], covers=[
    'input/hal/in2_int.h', 'input/hal/in2_ring.c', 'input/hal/in2_time.c',
    'kernel/time/tick.c', 'kernel/time/tick_clock.h', 'include/kbd.h',
    'tools/tests/input_ring_host.c', 'tools/tests/test_input_ring.py', 'tools/tests/host32.py'],
    basis=['INPUT_HAL_DESIGN §3, §4, §8', 'AV_HAL_DESIGN §5-2'], kind='contract')
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32
ROOT = Path(__file__).resolve().parents[2]

def run(ctx):
    ring = (ctx.root / 'input/hal/in2_ring.c').read_text()
    clock = (ctx.root / 'input/hal/in2_time.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-ring-') as td:
        tmp = Path(td)
        (tmp/'io.h').write_text('static inline unsigned irq_save(void) { return 0; }\nstatic inline void irq_restore(unsigned x) { (void)x; }\n')
        def build(r, t):
            (tmp/'in2_ring.c').write_text(r)
            (tmp/'in2_time.c').write_text(t)
            cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', '-I'+str(tmp), '-I'+str(ctx.root/'input/hal'), '-I'+str(ctx.root), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')]
            subprocess.run(cmd + [str(tmp/'in2_ring.c'), str(tmp/'in2_time.c'), str(ctx.root/'kernel/time/tick.c'), str(ctx.root/'tools/tests/input_ring_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        rc = build(ring, clock)
        assert rc == 0, rc
        print('PASS x86 ring and clock', flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
        if cc.is_file() and os.access(cc, os.X_OK):
            for f in [tmp/'in2_ring.c', tmp/'in2_time.c', ctx.root/'tools/tests/input_ring_host.c']:
                subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-DPPC_COMPILE', '-I'+str(tmp), '-I'+str(ctx.root/'input/hal'), '-I'+str(ctx.root), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32'), '-c', str(f), '-o', str(tmp/(f.stem+'.o'))], check=True)
            print('PASS PowerPC cross compile', flush=True)
        else:
            print('SKIP PowerPC cross compile: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1': return 1
        if ctx.mutate:
            mutations = [ ('count == IN2_RING_CAPACITY', 'count == IN2_RING_CAPACITY - 1'), ('++dropped;', '(void)0;'), ('events[i].src == e->src', '1'), ('e->sc == KEY_STOP', 'e->sc != KEY_STOP') ]
            for old, new in mutations:
                assert ring.count(old) == 1
                assert build(ring.replace(old, new), clock) != 0
                print('RED runtime:', old, flush=True)
            assert build(ring, clock.replace('tick_clock_ms()', '(tick_clock_ms() / 10)')) != 0
            print('RED runtime: tick as ms', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
