#!/usr/bin/env python3
CHECK = dict(name='gf-v2-g4-supplement-host', tier=1, needs=[], budget=40,
    covers=['hwdev/gf/**', 'drivers/video/geforce/*', 'arch/x86/video/gf_accept_cpu.c',
            'tools/accept/gf_accel.py', 'tools/tests/gf_accel*', 'tools/tests/*.py',
            'tools/tests/recover_gf_dev_stub.c', 'userland/gshell/src/**',
            'hwdev/gf/accel_fill.c', 'hwdev/gf/accel_copy.c',
            'hwdev/gf/accel_scale.c', 'hwdev/gf/*.h',
            'tools/tests/gf_accel_host.h', 'tools/tests/gf_accel_b3_host.c',
            'tools/tests/gf_accel_b4_host.c', 'tools/tests/gf_accel_b9_host.c',
            'tools/tests/test_gf_v2_g4_supplement.py', 'tools/tests/test_gf_accel.py'])

def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_v2_g4_supplement.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
