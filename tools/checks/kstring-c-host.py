#!/usr/bin/env python3
CHECK = dict(name='kstring-c-host', tier=1,
    covers=['tools/tests/host32.py', 'lib/kstring_asm.asm', 'lib/kstring_c.c', 'include/types.h', 'include/cpu.h', 'arch/x86/arch_cpu.h', 'arch/x86/x86_flags.h', 'tools/tests/kstring_c_host.c', 'tools/tests/test_kstring_c.py', 'tools/tests/mutpar.py', 'tools/check_map.d/check-kstring-c-host.yaml', 'build/checks.d/check-kstring-c-host.mk'],
    needs=[], budget=2.268254)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kstring_c.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
