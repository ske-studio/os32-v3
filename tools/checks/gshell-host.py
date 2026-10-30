#!/usr/bin/env python3
CHECK = dict(name='gshell-host', tier=1,
    covers=['userland/gshell/**', 'sdk/rust/os32api/**', 'sdk/rust/os32api/src/ls.rs', 'tools/tests/os32api_host.py', 'tools/check_map.d/check-gshell-host.yaml', 'build/checks.d/check-gshell-host.mk', 'exec/shell_service.h', 'userland/lib/font/**', 'sdk/include/os32/os32_kapi_shared.h', 'tools/fetch_fonts.py', 'tools/tests/g16_fixture.py', 'tools/gen_g16.py', 'tools/g16/**', 'assets/ipadic/*.csv'],
    needs=[], budget=11.616618)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'userland/gshell/host/integration.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
