#!/usr/bin/env python3
CHECK = dict(name='h4-manifest-host', tier=1,
    covers=['assets/profile_fdd', 'build/core.yaml', 'docs/manpages/hsync.1', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/gen_build_id.py', 'tools/hostdrv_deploy.py', 'tools/tests/h4_manifest_host.c', 'tools/tests/test_h4_manifest.py', 'tools/tests/test_hostdrv_manifest.py', 'userland/deploy.yaml', 'userland/system/hsync.c', 'lib/crc32_core.inc', 'userland/system/hsync_bootold.inc', 'userland/system/hsync_protect.inc', 'tools/tests/mutpar.py', 'tools/check_map.d/check-h4-manifest-host.yaml', 'build/checks.d/check-h4-manifest-host.mk', 'tools/tests/hsync_shell_fixture.h', 'sdk/include/os32/os32_generations.h', 'tools/tests/fdrecov_cases.inc'],
    needs=['make:all'], budget=4.866188)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_h4_manifest.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_hostdrv_manifest.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
