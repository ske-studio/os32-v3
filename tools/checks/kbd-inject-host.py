#!/usr/bin/env python3
CHECK = dict(name='kbd-inject-host', tier=1,
    covers=['tools/tests/host32.py', 'kernel/kbd_inject.c', 'tools/tests/kbd_inject_host.c', 'tools/tests/test_kbd_inject.py', 'kernel/console/con_sink.c', 'tools/check_map.d/check-kbd-inject-host.yaml', 'build/checks.d/check-kbd-inject-host.mk', 'exec/appslot.h', 'include/kbd_inject.h', 'include/con_sink.h', 'include/launch.h', 'kdebug/selftest_kbd_inject.c', 'include/kselftest_probe.h', 'kdebug/selftest_internal.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=1.386072)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kbd_inject.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
