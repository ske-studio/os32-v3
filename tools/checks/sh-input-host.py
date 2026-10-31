#!/usr/bin/env python3
CHECK = dict(name='sh-input-host', tier=1,
    covers=['tools/tests/host32.py', 'drivers/block/pc98pt.c', 'drivers/pci/pci_decode.c', 'tools/tests/sh_status_host.c', 'tools/tests/test_sh_status.py', 'userland/shell/cmd_base.c', 'userland/shell/cmd_dir.c', 'userland/shell/cmd_env.c', 'userland/shell/cmd_file.c', 'userland/shell/cmd_filer.c', 'userland/shell/cmd_fs_shared.c', 'userland/shell/cmd_hdprep.c', 'userland/shell/cmd_mnt.c', 'userland/shell/cmd_pci.c', 'userland/shell/cmd_script.c', 'userland/shell/cmd_sys.c', 'userland/shell/hdprep_plan.c', 'userland/shell/kbd_watch.c', 'userland/shell/main.c', 'userland/shell/pci_verbose.c', 'userland/shell/rshell.c', 'userland/shell/serial_watchdog.c', 'userland/shell/sh_exec.inc', 'userland/shell/ui.c', 'userland/shell/sh_args.inc', 'userland/shell/sh_launch.inc', 'userland/shell/sh_ls.inc', 'userland/shell/sh_pipe.inc', 'userland/shell/sh_redraw.inc', 'tools/tests/mutpar.py', 'init/kernel.c', 'tools/tests/sh_truncation_host.c', 'tools/tests/test_sh_truncation.py', 'tools/run_host_group.py', 'platform/pc98/boot.c', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc', 'init/kernel_gui_shlib.inc', 'init/kernel_shell_fdd.inc'],
    needs=[], budget=15.590698)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/run_host_group.py', *shlex.split(('--mutate' if ctx.mutate else '')), '--plain', 'tools/tests/test_sh_status.py', '--command', 'tools/tests/test_sh_truncation.py ' + ('--mutate-rshell' if ctx.mutate else '')]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
