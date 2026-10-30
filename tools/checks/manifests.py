#!/usr/bin/env python3
CHECK = dict(name='manifests', tier=0,
    covers=['apps', 'game', 'userland/**', 'docs/manpages/**', 'assets/**', 'build/app.conf', 'build/core.yaml', 'build/packages.yaml', 'docs/07_shell.md', 'tools/check_manifests.py', 'tools/deploy_manifests.py', 'CLAUDE.md', 'tools/check_map.d/check-manifests.yaml', 'build/checks.d/check-manifests.mk', 'build/programs.mk', 'tools/check_artifacts.py', 'include/config.h', 'build/sdk/crt/**', 'build/out/lib/**', 'build/sdk/lib/**', 'sdk/crt/**', 'sdk/gen_kapi.py'],
    needs=['make:all'], budget=3.413684)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/check_manifests.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
