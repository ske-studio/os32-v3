#!/usr/bin/env python3
CHECK = dict(name='layers-host', tier=1, needs=[], budget=5,
    covers=['tools/check_layers.py', 'tools/tests/test_layers.py', 'tools/layers_baseline.json',
            'kapi/kapi_generated.c'],
    basis=['LAYER_GUARD_DESIGN §4', 'LAYER_GUARD_DESIGN §5'],
)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_layers.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
