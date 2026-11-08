CHECK = dict(
    name='x87-host', tier=1,
    covers=['arch/x86/kernel/x87.c', 'arch/x86/kernel/x87.h',
            'tools/tests/x87_host.c', 'tools/tests/test_x87.py', 'tools/tests/host32.py'],
    needs=[], budget=2.6)


def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = ['python3', '-B', 'tools/tests/test_x87.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env)
