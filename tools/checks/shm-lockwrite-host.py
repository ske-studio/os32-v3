#!/usr/bin/env python3
CHECK = dict(name='shm-lockwrite-host', tier=1,
    covers=['userland/diag/shm_reuse_child.c', 'userland/diag/ring3_marker.h', 'tools/tests/test_shm_lockwrite.py', 'tools/guest_acceptance.yaml'],
    needs=[], budget=2.7,
    kind='characterization',
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_shm_lockwrite.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
