#!/usr/bin/env python3
CHECK = dict(name='ppc-sh-runtime', tier=1, needs=[], budget=10,
    covers=['userland/ppc/sh/*', 'tools/tests/ppc_sh_runtime.py','sdk/include/os32/os32api.h', 'sdk/include/os32/os32_kapi_shared.h',
            'sdk/include/os32/os32_kapi_generated.h'])


def run(ctx):
    import subprocess
    argv = ['python3', '-B', 'tools/tests/ppc_sh_runtime.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)
