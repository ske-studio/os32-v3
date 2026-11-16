#!/usr/bin/env python3
"""M6[2] interrupt capture, coalescing, and generation checks."""
CHECK = dict(name='input-m6-2-host', tier=1, budget=3, needs=[],
    basis=['docs/decisions/INPUT_BIND_DESIGN.md'],
    covers=['input/hal/input_interrupt.c', 'input/hal/input_context.c',
            'input/hal/input_context.h', 'tools/tests/input_m6_2_host.c',
            'tools/tests/test_input_m6_2.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'include/**'])
from pathlib import Path
import subprocess, sys, tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
def run(ctx):
    source = (ctx.root / 'input/hal/input_interrupt.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-m6-2-') as directory:
        tmp = Path(directory)
        (tmp / 'context.c').write_text((ctx.root / 'input/hal/input_context.c').read_text())
        def execute(code):
            (tmp / 'interrupt.c').write_text(code)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdinc', '-nostdlib',
                '-static', '-no-pie', '-I'+str(tmp), '-I'+str(ctx.root/'tools/tests/host_arch'),
                '-I'+str(ctx.root/'input/hal'), *include_flags(ctx.root),
                str(ctx.root/'tools/tests/input_m6_2_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(source) == 0
        if ctx.mutate:
            for old, new in [('interrupt_pending.owner == current->owner', 'interrupt_pending.owner != current->owner'),
                             ('interrupt_pending.exec_gen == current->exec_gen', 'interrupt_pending.exec_gen != current->exec_gen'),
                             ('interrupt_pending.command_gen == current->command_gen', 'interrupt_pending.command_gen != current->command_gen'),
                             ('        ++coalesced_count;', '        coalesced_count += 2;'),
                             ('if (matches) *reason = interrupt_pending.reason;', 'if (matches) *reason = 0;')]:
                assert source.count(old) == 1
                assert execute(source.replace(old, new, 1)) == 1
                print('RED runtime: ' + old, flush=True)
        print('PASS M6[2] capture and take', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
