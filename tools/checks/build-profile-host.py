#!/usr/bin/env python3
CHECK = dict(name='build-profile-host', tier=1,
    covers=['tools/build_profile.py', 'Makefile', 'build/**/*.mk', 'tools/tests/test_build_profile.py'], needs=[], budget=2.6)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_build_profile.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
