#!/usr/bin/env python3
CHECK = dict(name='fs-kind-host', tier=1,
    covers=['tools/tests/fs_kind_host.c', 'tools/tests/test_fs_kind.py', 'userland/shell/cmd_file.c', 'userland/shell/cmd_fs_shared.c', 'tools/check_map.d/check-fs-kind-host.yaml', 'build/checks.d/check-fs-kind-host.mk', 'tools/tests/fs_kind_callers_host.c', 'tools/tests/test_fs_kind_callers.py', 'tools/tests/mutpar.py', 'fs/vfs.c', 'fs/vfs_fd.c', 'tools/tests/test_vfs_kind.py', 'tools/tests/vfs_kind_host.c', 'tools/run_host_group.py', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=5.184744)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/run_host_group.py', *shlex.split(('--mutate' if ctx.mutate else '')), '--command', 'tools/tests/test_fs_kind.py --target', '--command', 'tools/tests/test_fs_kind_callers.py --target ' + ('--mutate' if ctx.mutate else ''), '--command', 'tools/tests/test_vfs_kind.py --target']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
