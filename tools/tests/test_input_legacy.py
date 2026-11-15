#!/usr/bin/env python3
"""Cooked ring against the fixed pre-move oracle, without host libc."""
CHECK = dict(name='input-legacy-host', tier=1, budget=3, needs=[], kind='characterization',
    covers=['input/hal/legacy_ring.c', 'input/hal/in2_int.h', 'drivers/input/*.h',
            'include/**', 'sdk/include/os32/**', 'tools/tests/host_arch/**',
            'platform/pc98/platform_io.h', 'build/config.mk', 'tools/tests/kernel_includes.py', 'tools/tests/host32.py',
            'tools/tests/input_legacy_host.c', 'tools/tests/test_input_legacy.py'])
from pathlib import Path
import subprocess, sys, tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
def run(ctx):
    source = (ctx.root/'input/hal/legacy_ring.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-legacy-') as directory:
        tmp = Path(directory)
        def execute(code):
            (tmp/'legacy_source.inc').write_text(code)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdinc', '-nostdlib',
                '-static', '-no-pie', '-I'+str(tmp), '-I'+str(ctx.root/'tools/tests/host_arch'),
                '-I'+str(ctx.root/'input/hal'), *include_flags(ctx.root),
                str(ctx.root/'tools/tests/input_legacy_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(source)==0
        if ctx.mutate:
            for old,new in [('kbd_count < KBD_BUF_SIZE', 'kbd_count < KBD_BUF_SIZE - 1'),
                            ('kbd_dropped++;', 'kbd_dropped += 0;'),
                            ('(kbd_tail + 1) % KBD_BUF_SIZE', '(kbd_tail + 2) % KBD_BUF_SIZE'),
                            ('entry = kbd_buf[kbd_head];', 'entry = kbd_buf[kbd_head]; kbd_count--;'),
                            ('kbd_raw_count < KBD_BUF_SIZE', 'kbd_raw_count < KBD_BUF_SIZE - 1'),
                            ('kbd_raw_dropped++;', 'kbd_raw_dropped += 0;'),
                            ('(kbd_raw_tail + 1) % KBD_BUF_SIZE', '(kbd_raw_tail + 2) % KBD_BUF_SIZE')]:
                assert source.count(old)==1, old
                assert execute(source.replace(old,new,1))==1, old
                print('RED runtime: '+old, flush=True)
                assert execute(source)==0
        print('PASS cooked oracle: empty/single/full/wrap/dropped/peek/flush/IRQ', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
