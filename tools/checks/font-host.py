#!/usr/bin/env python3
CHECK = dict(name='font-host', tier=1,
    covers=['userland/lib/font/**', 'tools/fetch_fonts.py', 'assets/fonts/IPA_Font_License_Agreement_v1.0.txt', 'tools/tests/test_font_reader.py', 'tools/tests/test_font_outline.py', 'userland/rust/font_test/**', 'userland/rust/Cargo.lock', 'sdk/rust/i686-os32-none.json', 'tools/check_isa386.py', 'tools/check_privileged.py', 'tools/check_privileged_allow.txt', 'tools/tests/test_font_raster.py', 'tools/tests/host32.py', 'tools/gen_g16.py', 'tools/g16/**', 'assets/ipadic/*.csv', 'tools/tests/test_font_cache.py', 'tools/tests/test_font_engine.py', 'tools/tests/g16_fixture.py', 'tools/run_host_group.py'],
    needs=['make:all'], budget=105.263134)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/run_host_group.py', *shlex.split(('--mutate' if ctx.mutate else '')), '--plain', 'tools/tests/test_font_reader.py', '--plain', 'tools/tests/test_font_outline.py', '--plain', 'tools/tests/test_font_raster.py', '--plain', 'tools/tests/test_font_cache.py', '--plain', 'tools/tests/test_font_engine.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
