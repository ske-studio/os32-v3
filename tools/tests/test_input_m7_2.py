#!/usr/bin/env python3
"""M7[2] cancellation pending."""
CHECK = dict(name='input-m7-2-host', tier=1, budget=3, needs=[],
    basis=['docs/decisions/INPUT_BIND_DESIGN.md'],
    covers=['sys/input/input_ime_gate.c',
            'sys/input/input_context.h', 'tools/tests/input_m7_2_host.c',
            'tools/tests/test_input_m7_2.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'include/**', 'sys/kern/exec/appslot.h', 'sys/kern/mm/pgalloc.h',
            'tools/tests/input_display_stubs.h', 'tools/tests/host_arch/**'])
from pathlib import Path
import subprocess, sys, tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
def run(ctx):
    source = (ctx.root / 'sys/input/input_ime_gate.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-m7-2-') as directory:
        tmp = Path(directory)
        def execute(code):
            (tmp / 'ime_gate.c').write_text(code)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdinc', '-nostdlib',
                '-static', '-no-pie', '-I'+str(tmp), '-I'+str(ctx.root/'tools/tests/host_arch'),
                '-I'+str(ctx.root/'input/hal'), *include_flags(ctx.root),
                str(ctx.root/'tools/tests/input_m7_2_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(source) == 0
        if ctx.mutate:
            for old, new in [('    if (count == 8)', '    if (count) count = 0;\n    if (count == 8)'),
                             ('        ++dropped;', '        ++dropped; ++head; head %= 8;'),
                             ('actions[head].after == popped', '((void)popped, 1)'),
                             ('entry.command_gen == now->command_gen', '1'),
                             ('                  cancel_exec_gen == current->exec_gen;', '                  1;'),
                             ('        ++cancel_coalesced;', '        cancel_coalesced += 2;'),
                             ('        cancel_owner = destination->owner;', '        cancel_owner = destination->owner + 1;')]:
                assert source.count(old) == 1
                assert execute(source.replace(old, new, 1)) == 1
                print('RED runtime: ' + old, flush=True)
        print('PASS M7[2] cancellation', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
