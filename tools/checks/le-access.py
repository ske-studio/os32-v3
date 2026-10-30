#!/usr/bin/env python3
CHECK = dict(name='le-access', tier=1,
    covers=['fs/ext2/ext2_fmt_*.inc', 'tools/clang_ast/**', 'tools/gen_hwdev_pair.py', 'tools/gen_sqlite_imports.py', 'tools/prototypes/cd_imports.def', 'tools/prototypes/pcm_imports.def', 'tools/prototypes/lan_imports.def', 'tools/prototypes/cirrus_imports.def', 'tools/tests/test_clang_ast.py', 'drivers/**/*.[ch]', 'fs/**/*.[ch]', 'lib/**/*.[ch]', 'kernel/**/*.[ch]', 'include/**', 'tools/check_arm_compile.py', 'tools/check_le_access.py', 'tools/check_map.d/check-le-access.yaml', 'build/checks.d/check-le-access.mk'],
    needs=[], budget=5.442474)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/check_le_access.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
