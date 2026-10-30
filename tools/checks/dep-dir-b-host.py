#!/usr/bin/env python3
CHECK = dict(name='dep-dir-b-host', tier=1,
    covers=['kernel/sys.c', 'include/platform_sys.h', 'platform/pc98/system.c', 'include/pc98.h', 'tools/tests/dep_dir_b_io_host.c', 'tools/tests/dep_dir_b_io_shim.h', 'tools/tests/dep_dir_b_l11_baseline.json', 'tools/tests/test_dep_dir_b_io.py', 'tools/tests/host32.py', 'tools/check_map.d/check-dep-dir-b-host.yaml', 'build/checks.d/check-dep-dir-b-host.mk', 'include/platform_irq.h', 'platform/pc98/pic.c', 'arch/x86/kernel/idt.c', 'arch/x86/kernel/idt.h', 'kernel/irq/irq.c', 'kernel/irq/irq_math.c', 'kernel/irq/irq_math.h', 'tools/tests/test_dep_dir_b_pic.py', 'tools/tests/dep_dir_b_pic_host.c', 'tools/tests/dep_dir_b_l12_baseline.json', 'include/platform_timer.h', 'platform/pc98/clock.c', 'kernel/time/pit.c', 'platform/pc98/timer.c', 'kernel/time/sysclk.c', 'kernel/time/sysclk.h', 'kernel/time/ktime.c', 'kernel/time/ktime.h', 'kernel/time/pit_math.c', 'kernel/time/pit_math.h', 'kernel/time/time_math.c', 'kernel/time/time_math.h', 'include/types.h', 'include/memmap.h', 'include/time_units.h', 'kernel/v86/v86_bios.h', 'tools/tests/test_dep_dir_b_timer.py', 'tools/tests/dep_dir_b_timer_host.c', 'tools/tests/dep_dir_b_l13_baseline.json', 'tools/tests/kernel_includes.py'],
    needs=[], budget=60.0)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_b_io.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_b_pic.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_b_timer.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
