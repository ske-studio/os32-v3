#!/usr/bin/env python3
CHECK = dict(name='d2-services-host', tier=1,
    covers=['tools/tests/test_d2_services.py', 'tools/tests/d2_services_host.c', 'tools/tests/host32.py', 'include/input_service.h', 'include/kbd.h', 'include/types.h', 'drivers/input/kbd.h', 'kernel/input_service.c', 'include/pointer_service.h', 'platform/pc98/d2_bind.c', 'init/kernel.c', 'kernel/ime/fep_module.c', 'kernel/ime/ime.c', 'kernel/gui.c', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'exec/exec.c', 'exec/exec_image.inc', 'fs/vfs_fd.c', 'sdk/kapi.json', 'sdk/gen_kapi.py', 'kapi/kapi_generated.c', 'sdk/include/os32/os32_kapi_shared.h', 'exec/exec_kapi_init.inc', 'drivers/input/mouse.h', 'tools/tests/pointer_fixture_host.h', 'include/port_service.h', 'include/console_output.h', 'kernel/port_service.c', 'kernel/serial_diag.c', 'lib/serial_diag.h', 'drivers/char/serial.h', 'tools/tests/port_fixture_host.h', 'kapi/kapi_sys.c', 'kernel/console/console.c', 'kernel/ver_collect.c', 'arch/x86/kernel/isr_handlers.c', 'arch/x86/kernel/double_fault.c', 'include/clock_service.h', 'kernel/time/clock_service.c', 'drivers/char/rtc.h', 'drivers/char/rtc.c', 'kernel/sys.c', 'fs/fat/fatfs/diskio.c', 'tools/tests/clock_fixture_host.h', 'lib/os_time.c', 'include/os_time.h', 'init/boot_platform.h', 'init/boot_platform.c', 'init/boot_display.h', 'init/boot_display.c', 'platform/pc98/boot.h', 'platform/pc98/boot.c', 'kernel/console/console_backend.h', 'platform/pc98/console.c', 'include/console_attr.h', 'tools/tests/kernel_includes.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc'],
    needs=[], budget=60.0)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_d2_services.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
