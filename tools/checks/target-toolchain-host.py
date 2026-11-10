#!/usr/bin/env python3
CHECK = dict(name='target-toolchain-host', tier=1,
    covers=['tools/tests/build_fixture.py', 'sdk/target_profile.py', 'sdk/target_profile_cli.py', 'tools/build_profile.py', 'build/config.mk', 'build/arch/*.mk', 'tools/tests/test_target_toolchain.py'], needs=[], budget=2.7)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_target_toolchain.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
