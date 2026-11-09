#!/usr/bin/env python3
CHECK = dict(name='sep5-boot-host', tier=1,
    covers=['tools/tests/test_sep5_boot.py', 'tools/checks/sep5-boot-host.py',
            'build/**/*.mk', 'boot/*', 'Makefile', 'build/config.mk'],
    needs=[], budget=10)


def run(ctx):
    import subprocess
    argv = ['python3', '-B', 'tools/tests/test_sep5_boot.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)
