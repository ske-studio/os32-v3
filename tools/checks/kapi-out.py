#!/usr/bin/env python3
CHECK = dict(name='kapi-out', tier=1,
    covers=['kapi/kapi_generated.c', 'sdk/gen_kapi.py', 'tools/tests/test_kapi_out.py', 'tools/check_map.d/check-kapi-out.yaml', 'build/checks.d/check-kapi-out.mk', 'sdk/kapi.json', 'tools/tests/test_gen_kapi_cli.py', 'build/os32.ld', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld'],
    needs=['make:all'], budget=2.891648)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kapi_out.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_gen_kapi_cli.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
