#!/usr/bin/env python3
CHECK = dict(name='gfx2-exec-host', tier=1,
    covers=['exec/exec_finish.inc', 'gfx/v2/gfx2_exec.h', 'tools/tests/gfx2_exec_host.c', 'tools/tests/test_gfx2_exec.py', 'tools/tests/host32.py'],
    needs=[], budget=5.0)

def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_exec.py'] + (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env, close_fds=False)
