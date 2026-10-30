#!/usr/bin/env python3
CHECK = dict(name='deploy-set-host', tier=1,
    covers=['sdk/kapi.json', 'tools/gen_deploy_set.py', 'tools/deploy_source_check.py', 'tools/deploy_protect.py', 'tools/nhd_deploy.py', 'tools/check_manifests.py', 'tools/deploy_manifests.py', 'tools/check_artifacts.py', 'tools/tests/test_deploy_set.py', 'sdk/os32x_hdr.py', 'sdk/os32_generations.py', 'build/core.yaml', 'userland/deploy.yaml', 'CLAUDE.md', 'apps', 'game', 'assets/filetypes', 'build/app.conf', 'build/packages.yaml', 'build/programs.mk', 'docs/07_shell.md', 'tools/nhd_profile.py', 'tools/pc98pt.py', 'userland/system/hsync.c', 'userland/system/hsync_protect.inc', 'userland/system/hsync_bootold.inc', 'lib/crc32_core.inc', 'build/sdk/crt/**', 'build/out/lib/**', 'build/sdk/lib/**', 'sdk/crt/**', 'sdk/gen_kapi.py'],
    needs=['make:all'], budget=6.290696)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_deploy_set.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
