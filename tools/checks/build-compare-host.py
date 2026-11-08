#!/usr/bin/env python3
CHECK = dict(name='build-compare-host', tier=1,
    covers=['tools/build_compare.py', 'tools/build_compare.sh', 'tools/gen_build_id.py', 'tools/tests/test_build_compare.py'], needs=[], budget=2.8)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_build_compare.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
