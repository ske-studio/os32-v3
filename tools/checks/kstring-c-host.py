#!/usr/bin/env python3
CHECK = dict(name='kstring-c-host', tier=4,
    covers=['sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'tools/tests/host32.py', 'sys/kern/kstring_asm.asm', 'sys/kern/kstring_c.c', 'sys/include/types.h', 'sys/include/cpu.h', 'sys/arch/x86/arch_cpu.h', 'sys/arch/x86/x86_flags.h', 'tools/tests/kstring_c_host.c', 'tools/tests/test_kstring_c.py', 'tools/tests/mutpar.py'],
    needs=[], budget=3.5,
    kind='characterization',
)


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
