#!/usr/bin/env python3
CHECK = dict(name='hsync-host', tier=1,
    covers=['fs/hostdrv/hostdrvfs.c', 'lib/crc32.c', 'tools/tests/hsync_h1_host.c', 'tools/tests/test_hsync_h1.py', 'userland/system/hsync.c', 'tools/tests/hsync_shell_fixture.h', 'sdk/include/os32/os32_generations.h', 'build/app.conf', 'docs/manpages/hsync.1', 'tools/tests/hsync_h2_host.c', 'tools/tests/test_hsync_h2.py', 'tools/tests/mutpar.py', 'fs/ext2/ext2_vfs.c', 'fs/fat/fatfs_vfs.c', 'fs/isofs/iso9660.c', 'fs/vfs.c', 'kapi/kapi_sys.c', 'tools/tests/hsync_h3_host.c', 'tools/tests/test_hsync_h3.py', 'tools/tests/vfs_set_mtime_host.c', 'sdk/kapi.json', 'tools/run_host_group.py', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=9.368792)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/run_host_group.py', *shlex.split(('--mutate' if ctx.mutate else '')), '--command', 'tools/tests/test_hsync_h1.py --target', '--command', 'tools/tests/test_hsync_h2.py --target ' + ('--mutate' if ctx.mutate else ''), '--command', 'tools/tests/test_hsync_h3.py --target ' + ('--mutate' if ctx.mutate else '')]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
