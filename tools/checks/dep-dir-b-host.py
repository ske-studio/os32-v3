#!/usr/bin/env python3
CHECK = dict(name='dep-dir-b-host', tier=1,
    covers=['sys/platform/pc98/clock_source.h', 'sys/kern/io_clock.h', 'sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'sys/hal/time/clock_source.h', 'sys/platform/pc98/clock_source.c', 'sys/platform/pc98/pit_time.c', 'sys/platform/pc98/pit_time.h', 'sys/platform/pc98/irq_policy.c', 'sys/platform/pc98/irq_complete.c', 'sys/platform/pc98/platform_irq_limits.h', 'sys/include/kernel_tick.h', 'sys/kern/sys.c', 'sys/include/platform_sys.h', 'sys/platform/pc98/system.c', 'sys/platform/pc98/pc98.h', 'tools/tests/dep_dir_b_io_host.c', 'tools/tests/dep_dir_b_io_shim.h', 'tools/tests/dep_dir_b_l11_baseline.json', 'tools/tests/test_dep_dir_b_io.py', 'tools/tests/host32.py', 'sys/include/platform_irq.h', 'sys/platform/pc98/pic.c', 'sys/arch/x86/kernel/idt.c', 'sys/arch/x86/kernel/idt.h', 'sys/platform/pc98/pit.h', 'sys/include/kernel_tick.h', 'sys/kern/irq/irq.c', 'sys/kern/irq/irq_math.c', 'sys/kern/irq/irq_math.h', 'tools/tests/test_dep_dir_b_pic.py', 'tools/tests/dep_dir_b_pic_host.c', 'tools/tests/dep_dir_b_l12_baseline.json', 'sys/include/platform_timer.h', 'sys/platform/pc98/clock.c', 'sys/platform/pc98/pit.c', 'sys/platform/pc98/timer.c', 'sys/platform/pc98/sysclk.c', 'sys/platform/pc98/sysclk.h', 'sys/kern/time/ktime.c', 'sys/kern/time/ktime.h', 'sys/hal/time/tick_clock.h', 'sys/platform/pc98/pit_math.c', 'sys/platform/pc98/pit_math.h', 'sys/kern/time/time_math.c', 'sys/kern/time/time_math.h', 'sys/include/types.h', 'sys/include/memmap.h', 'sys/include/time_units.h', 'sys/platform/pc98/legacy/v86_bios.h', 'sys/platform/pc98/bios_snapshot.h', 'tools/tests/test_dep_dir_b_timer.py', 'tools/tests/dep_dir_b_timer_host.c', 'tools/tests/dep_dir_b_l13_baseline.json', 'tools/tests/kernel_includes.py'],
    needs=[], budget=4.2,
    kind='characterization',
)


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
