#!/usr/bin/env python3
CHECK = dict(name='shm-lockwrite-host', tier=1,
    covers=['userland/tests/shm_reuse_child.c', 'userland/tests/ring3_marker.h', 'tools/tests/test_shm_lockwrite.py', 'tools/tests/guest_acceptance.yaml', 'tools/check_map.d/check-shm-lockwrite-host.yaml'],
    needs=[], budget=1.096908)


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
