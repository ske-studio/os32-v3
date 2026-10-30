#!/usr/bin/env python3
CHECK = dict(name='multiapp-model-host', tier=1,
    covers=['tools/tests/host32.py', 'exec/appslot.c', 'fs/fd_redirect.c', 'fs/pipe_buffer.c', 'mm/shm.c', 'tools/tests/multiapp_impl_host.c', 'tools/tests/multiapp_model_host.c', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_multiapp_impl.py', 'tools/tests/test_multiapp_model.py', 'tools/tests/test_owner_reclaim.py', 'exec/launch.c', 'kernel/kbd_inject.c', 'tools/check_map.d/check-multiapp-model-host.yaml', 'build/checks.d/check-multiapp-model-host.mk', 'exec/exec.c', 'exec/exec_image.inc', 'exec/appslot.h', 'exec/exec_kapi_init.inc', 'tools/tests/kernel_includes.py'],
    needs=[], budget=4.246242)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_multiapp_model.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_multiapp_impl.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    env['MUTATE'] = ctx.make_vars.get('MUTATE', '1' if ctx.mutate else '0')
    argv = ['python3', '-B', 'tools/tests/test_owner_reclaim.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
