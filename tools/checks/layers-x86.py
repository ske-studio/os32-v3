#!/usr/bin/env python3
"""x86-pc98 の像の .d から層の違反を数え、基準 (tools/layers_baseline.json) と照らす。"""
CHECK = dict(name='layers-x86', tier=1, needs=['make:all'], budget=20,
    covers=['tools/check_layers.py', 'tools/layers_baseline.json', 'build/**',
            'arch/**', 'platform/**', 'include/**', 'kernel/**', 'init/**', 'exec/**',
            'mm/**', 'fs/**', 'gfx/**', 'net/**', 'kapi/**', 'input/**', 'lib/**',
            'drivers/**', 'hwdev/**', 'kdebug/**', 'sdk/**'],
    basis=['LAYER_GUARD_DESIGN §4', 'LAYER_GUARD_DESIGN §5'],
)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/check_layers.py', '--image', 'x86-pc98'],
                           cwd=ctx.root)
