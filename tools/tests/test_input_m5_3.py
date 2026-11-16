#!/usr/bin/env python3
"""M5[3] owner registration and IRQ snapshot."""
CHECK = dict(name='input-m5-3-host', tier=1, budget=3, needs=[],
    basis=['docs/decisions/INPUT_BIND_DESIGN.md'],
    covers=['input/hal/input_context.c', 'input/hal/input_context.h',
            'tools/tests/input_m5_3_host.c', 'tools/tests/test_input_m5_3.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'include/**'])
from pathlib import Path
import subprocess, sys, tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
def run(ctx):
    source = (ctx.root / 'input/hal/input_context.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-m5-3-') as directory:
        tmp = Path(directory)
        def execute(code):
            (tmp / 'context.c').write_text(code)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdinc', '-nostdlib',
                '-static', '-no-pie', '-I'+str(tmp), '-I'+str(ctx.root/'tools/tests/host_arch'), '-I'+str(ctx.root/'input/hal'), *include_flags(ctx.root),
                str(ctx.root/'tools/tests/input_m5_3_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(source) == 0
        if ctx.mutate:
            for old, new in [('context_owners[owner].exec_gen == exec_gen;', 'context_owners[owner].exec_gen != 0;'),
                             ('next->parent = entry->parent;', 'next->parent = 0;'),
                             ('? entry->parent : 0;', '? 0 : 0;'),
                             ('out->command_gen = entry->policy.command_gen;', 'out->command_gen = 0;'),
                             ('INPUT_CONTEXT_ISIG | INPUT_CONTEXT_ICANON);\n    foreground_owner = APP_ID_SHELL;', 'INPUT_CONTEXT_ISIG | INPUT_CONTEXT_ICANON);\n    foreground_owner = 0;')]:
                assert source.count(old) == 1
                assert execute(source.replace(old, new, 1)) == 1
                print('RED runtime: ' + old, flush=True)
        print('PASS M5[3] owner context', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
