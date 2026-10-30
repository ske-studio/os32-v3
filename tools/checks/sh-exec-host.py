#!/usr/bin/env python3
CHECK = dict(name='sh-exec-host', tier=1,
    covers=['tools/tests/host32.py', 'tools/tests/sh_launch_host.c', 'tools/tests/test_sh_launch.py', 'userland/shell/sh_launch.inc', 'tools/check_map.d/check-sh-exec-host.yaml', 'build/checks.d/check-sh-exec-host.mk', 'tools/tests/sh_shell_host.c', 'tools/tests/test_sh_shell.py', 'userland/shell/cmd_env.c', 'userland/shell/cmd_file.c', 'userland/shell/cmd_fs_shared.c', 'userland/shell/cmd_mnt.c', 'userland/shell/cmd_script.c', 'userland/shell/cmd_sys.c', 'userland/shell/kbd_watch.c', 'userland/shell/sh_args.inc', 'userland/shell/sh_ls.inc', 'userland/shell/sh_pipe.inc', 'userland/shell/sh_redraw.inc', 'tools/run_host_group.py'],
    needs=[], budget=4.258386)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/run_host_group.py', *shlex.split(('--mutate' if ctx.mutate else '')), '--control', 'tools/tests/test_sh_launch.py', '--control', 'tools/tests/test_sh_shell.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
