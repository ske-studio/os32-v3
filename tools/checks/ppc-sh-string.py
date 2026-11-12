#!/usr/bin/env python3
CHECK = dict(name='ppc-sh-string', tier=1, budget=10, needs=[],
             covers=['userland/ppc/sh/string.c', 'userland/ppc/sh/include/string.h',
                     'tools/tests/ppc_sh_string.py', 'tools/checks/ppc-sh-string.py'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/ppc_sh_string.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
