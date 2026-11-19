#!/usr/bin/env python3
CHECK = dict(name='d2-services-host', tier=1,
    covers=['include/exec_wait_contract.h', 'drivers/char/serial_kapi.h', 'arch/x86/kernel/kapi_call.c', 'arch/x86/kernel/syscall_entry.c', 'tools/tests/syscall_source.py', 'include/callconv.h', 'include/abi.h', 'arch/x86/arch_abi.h', 'tools/tests/test_d2_services.py', 'tools/tests/d2_services_host.c', 'tools/tests/host32.py', 'include/input_service.h', 'include/kbd.h', 'include/types.h', 'drivers/input/kbd.h', 'kernel/input_service.c', 'include/pointer_service.h', 'platform/pc98/d2_bind.c', 'platform/pc98/kernel.c', 'kernel/ime/fep_module.c', 'kernel/ime/ime.c', 'kernel/gui.c', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'exec/exec.c', 'fs/vfs_fd.c', 'sdk/kapi.json', 'include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'kapi/kapi_generated.c', 'sdk/include/os32/os32_kapi_shared.h', 'drivers/input/mouse.h', 'tools/tests/pointer_fixture_host.h', 'include/port_service.h', 'include/console_output.h', 'kernel/port_service.c', 'kernel/serial_diag.c', 'lib/serial_diag.h', 'drivers/char/serial.h', 'tools/tests/port_fixture_host.h', 'kapi/kapi_sys.c', 'kernel/console/console.c', 'kernel/ver_collect.c', 'arch/x86/kernel/isr_handlers.c', 'arch/x86/kernel/double_fault.c', 'include/clock_service.h', 'kernel/time/clock_service.c', 'drivers/char/rtc.h', 'drivers/char/rtc.c', 'kernel/sys.c', 'drivers/block/fat_diskio.c', 'tools/tests/clock_fixture_host.h', 'lib/os_time.c', 'include/os_time.h', 'platform/pc98/boot_platform.h', 'platform/pc98/boot_platform.c', 'platform/pc98/boot_display.h', 'platform/pc98/boot_display.c', 'platform/pc98/boot.h', 'platform/pc98/boot.c', 'kernel/console/console_backend.h', 'platform/pc98/console.c', 'include/console_attr.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h', 'kapi/kapi_port.c'],
    needs=[], budget=5.5,
    basis=['AC-INPUT-1', 'AC-KAPI-1'],
)


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
