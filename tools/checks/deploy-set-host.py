#!/usr/bin/env python3
CHECK = dict(name='deploy-set-host', tier=1,
    covers=['exec/os32x_hdr.c', 'sdk/kapi.json', 'tools/gen_deploy_set.py', 'tools/deploy_source_check.py', 'tools/deploy_protect.py', 'tools/nhd_deploy.py', 'tools/np21w_ctl.py', 'tools/check_manifests.py', 'tools/deploy_manifests.py', 'tools/check_artifacts.py', 'tools/tests/test_deploy_set.py', 'sdk/os32x_hdr.py', 'sdk/os32_generations.py', 'build/core.yaml', 'userland/deploy.yaml', 'CLAUDE.md', 'apps', 'game', 'assets/filetypes', 'build/app.conf', 'build/packages.yaml', 'build/programs.mk', 'docs/07_shell.md', 'tools/nhd_profile.py', 'tools/pc98pt.py', 'userland/system/hsync.c', 'build/sdk/crt/**', 'build/out/lib/**', 'build/sdk/lib/**', 'sdk/crt/**', 'include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py'],
    needs=['make:all'], budget=6.7)


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
