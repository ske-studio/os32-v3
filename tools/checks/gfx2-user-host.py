#!/usr/bin/env python3
CHECK = dict(name='gfx2-user-host', tier=1, needs=[], budget=10,
    covers=['userland/lib/gfx/v2/**', 'gfx/v2/format.c', 'gfx/v2/gfx2_fmt.h',
            'include/types.h', 'include/callconv.h', 'include/memmap.h', 'sdk/include/os32/os32api.h',
            'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_generated.h',
            'sdk/include/os32/os32_gfx2.h',
            'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_host_notice.h',
            'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_surface.h',
            'sdk/include/os32/os32_ls.h', 'build/libs.mk',
            'tools/tests/gfx2_user_host.c', 'tools/tests/test_gfx2_user.py',
            'tools/tests/host32.py'])


def run(ctx):
    import os
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_gfx2_user.py', *(['--mutate'] if ctx.mutate else [])],
        cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
