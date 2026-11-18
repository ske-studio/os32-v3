#!/usr/bin/env python3
"""ppc-powermac の 4 像の .d から層の違反を数え、基準 (tools/layers_baseline.json) と照らす。"""
CHECK = dict(env=['ppc-cross'], name='layers-ppc', tier=1, needs=['make:ppc-ci-image'], budget=20,
    covers=['tools/check_layers.py', 'tools/layers_baseline.json', 'build/**',
            'tools/ppc/compile_ci.py', 'arch/**', 'platform/**', 'include/**', 'kernel/**',
            'init/**', 'exec/**', 'mm/**', 'fs/**', 'gfx/**', 'net/**', 'kapi/**', 'input/**',
            'lib/**', 'drivers/**', 'hwdev/**', 'sdk/**'],
    basis=['LAYER_GUARD_DESIGN §4', 'LAYER_GUARD_DESIGN §5'],
    env_limit='PowerPC の toolchain が無い host では像を組めないので数えない (OS32_REQUIRE_PPC=1 なら失敗)。',
)


def run(ctx):
    import os
    from pathlib import Path
    import subprocess
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file():
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP') + ' layers-ppc: PPC toolchain unavailable', flush=True)
        return 1 if required else 0
    return subprocess.call(['python3', '-B', 'tools/check_layers.py', '--image', 'ppc-powermac'],
                           cwd=ctx.root)
