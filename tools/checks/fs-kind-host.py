#!/usr/bin/env python3
CHECK = dict(name='fs-kind-host', tier=1,
    covers=['tools/tests/fs_kind_host.c', 'tools/tests/test_fs_kind.py', 'userland/shell/cmd_file.c', 'userland/shell/cmd_fs_shared.c', 'tools/tests/fs_kind_callers_host.c', 'tools/tests/test_fs_kind_callers.py', 'tools/tests/mutpar.py', 'sys/fs/vfs.c', 'sys/fs/vfs_fd.c', 'tools/tests/test_vfs_kind.py', 'tools/tests/vfs_kind_host.c', 'tools/run_host_group.py', 'tools/tests/input_fixture_host.h', 'sys/hal/input/input_service.c', 'sys/hal/input/pointer_service.h', 'sys/hal/input/input_service.h', 'sys/include/kbd.h', 'sys/fs/vfs_device.h', 'sys/platform/pc98/boot_names.h', 'sys/platform/pc98/boot_names.c', 'sys/dev/ata/ide.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=5.5,
    kind='characterization',
)


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
