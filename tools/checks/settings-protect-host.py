#!/usr/bin/env python3
CHECK = dict(name='settings-protect-host', tier=1,
    covers=['build/core.yaml', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/hostdrv_deploy.py', 'tools/nhd_deploy.py', 'tools/pc98pt.py', 'tools/prune_stale.py', 'tools/tests/hsync_protect_host.c', 'tools/tests/test_deploy_protect.py', 'tools/tests/test_hsync_protect.py', 'userland/deploy.yaml', 'userland/system/hsync.c', 'lib/crc32_core.inc', 'userland/system/hsync_bootold.inc', 'userland/system/hsync_protect.inc', 'tools/check_map.d/check-settings-protect-host.yaml', 'build/checks.d/check-settings-protect-host.mk', 'tools/nhd_profile.py', 'tools/gen_deploy_set.py', 'build/os32.ld', 'sdk/gen_kapi.py'],
    needs=['make:all'], budget=6.172034)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    env['MUTATE'] = ctx.make_vars.get('MUTATE', '1' if ctx.mutate else '0')
    argv = ['python3', '-B', 'tools/tests/test_deploy_protect.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_hsync_protect.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
