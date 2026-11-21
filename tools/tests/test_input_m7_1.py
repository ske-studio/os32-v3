#!/usr/bin/env python3
"""M7[1] IME state snapshot."""
CHECK = dict(name='input-m7-1-host', tier=1, budget=3, needs=[],
    basis=['docs/decisions/INPUT_BIND_DESIGN.md'],
    covers=['input/hal/input_ime_gate.c',
            'input/hal/input_context.h', 'tools/tests/input_m7_1_host.c',
            'tools/tests/test_input_m7_1.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'include/**', 'exec/appslot.h', 'mm/pgalloc.h',
            'tools/tests/input_display_stubs.h', 'tools/tests/host_arch/**'])
from pathlib import Path
import subprocess, sys, tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
def run(ctx):
    source = (ctx.root / 'input/hal/input_ime_gate.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-m7-1-') as directory:
        tmp = Path(directory)
        def execute(code):
            (tmp / 'ime_gate.c').write_text(code)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdinc', '-nostdlib',
                '-static', '-no-pie', '-I'+str(tmp), '-I'+str(ctx.root/'tools/tests/host_arch'),
                '-I'+str(ctx.root/'input/hal'), *include_flags(ctx.root),
                str(ctx.root/'tools/tests/input_m7_1_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(source) == 0
        if ctx.mutate:
            for old, new in [('active, owner, current.exec_gen, command_gen', 'active, owner + 1, current.exec_gen, command_gen'),
                             ('*out = ime_snapshot;', '*out = ime_snapshot; if (out->active) out->exec_gen = generation;'),
                             ('active && !input_context_snapshot(&current)', 'active && (input_context_snapshot(&current), 1)')]:
                assert source.count(old) == 1
                assert execute(source.replace(old, new, 1)) == 1
                print('RED runtime: ' + old, flush=True)
        print('PASS M7[1] snapshot', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
