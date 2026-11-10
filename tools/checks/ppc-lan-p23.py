#!/usr/bin/env python3
CHECK = dict(name='ppc-lan-p23', tier=1, budget=60, needs=[],
             covers=['tools/ppc_lan_host.py', 'tools/ppc/test_lan_p23.py', 'platform/powermac/lan_wire.c'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/ppc/test_lan_p23.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
