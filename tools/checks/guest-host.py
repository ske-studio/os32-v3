#!/usr/bin/env python3
CHECK = dict(name='guest-host', tier=1,
    covers=['tools/lanfs_host.py', 'tools/lan_bridge.py', 'tools/tests/guest_tests.txt', 'build/core.yaml', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/guest_tests.py', 'tools/hostdrv_deploy.py', 'tools/tests/test_guest_tests.py', 'userland/deploy.yaml', 'tools/tests/mutpar.py', 'tools/check_map.d/check-guest-host.yaml', 'build/checks.d/check-guest-host.mk', 'tools/tests/test_e9_observation.py', 'tools/tests/host32.py', 'userland/shell/rshell.c', 'userland/tests/ring3_marker.h', 'userland/tests/nop.c', 'userland/tests/ring3_hello.c', 'userland/tests/ring3_fault.c', 'userland/tests/ring3_guard.c', 'userland/tests/shm_reuse_test.c', 'userland/tests/shm_reuse_child.c', 'build/programs.mk', 'include/memmap.h', 'sdk/include/os32/os32_kapi_slots.h', 'tools/rshell_serial.py', 'tools/rshell_peek.py', 'tools/serialfs_host.py', 'tools/tvdump_recv.py'],
    needs=['make:all'], budget=13.256608)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_guest_tests.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_e9_observation.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
