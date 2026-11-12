#!/usr/bin/env python3
CHECK = dict(name='ppc-sh-input', tier=1, budget=60, needs=[],
    covers=['platform/powermac/terminal_input.c', 'platform/powermac/input_boot.c', 'platform/powermac/input_boot.h', 'platform/powermac/input_decode.c', 'tools/tests/ppc_sh_input.py'])

def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/ppc_sh_input.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
