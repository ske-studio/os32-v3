CHECK = dict(
    name='x87-lifecycle-host', tier=1,
    covers=['include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'exec/exec.c', 'exec/exec_*.inc', 'arch/x86/kernel/setjmp.asm',
            'arch/x86/kernel/cpu_flags.inc', 'tools/tests/x87_lifecycle_host.c',
            'tools/tests/test_x87_lifecycle.py', 'tools/tests/exec_source.py',
            'tools/tests/host_arch/*.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'sdk/gen_kapi.py'],
    needs=[], budget=4.1)


def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = ['python3', '-B', 'tools/tests/test_x87_lifecycle.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env)
