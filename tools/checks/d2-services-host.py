#!/usr/bin/env python3
CHECK = dict(name='d2-services-host', tier=1,
    covers=['sys/include/exec_wait_contract.h', 'sys/dev/char/serial_kapi.h', 'sys/arch/x86/kernel/kapi_call.c', 'sys/arch/x86/kernel/syscall_entry.c', 'tools/tests/syscall_source.py', 'sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'tools/tests/test_d2_services.py', 'tools/tests/d2_services_host.c', 'tools/tests/host32.py', 'sys/hal/input/input_service.h', 'sys/include/kbd.h', 'sys/include/types.h', 'sys/platform/pc98/input/kbd.h', 'sys/hal/input/input_service.c', 'sys/hal/input/pointer_service.h', 'sys/platform/pc98/d2_bind.c', 'sys/platform/pc98/kernel.c', 'sys/kern/ime/fep_module.c', 'sys/kern/ime/ime.c', 'sys/kern/gui.c', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'sys/kern/exec/exec.c', 'sys/fs/vfs_fd.c', 'sdk/kapi.json', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/kapi/kapi_generated.c', 'sdk/include/os32/os32_kapi_shared.h', 'sys/platform/pc98/input/mouse.h', 'tools/tests/pointer_fixture_host.h', 'sys/include/port_service.h', 'sys/include/console_output.h', 'sys/kern/port_service.c', 'sys/kern/serial_diag.c', 'lib/serial_diag.h', 'sys/dev/char/serial.h', 'tools/tests/port_fixture_host.h', 'sys/kapi/kapi_sys.c', 'sys/kern/console/console.c', 'sys/kern/ver_collect.c', 'sys/arch/x86/kernel/isr_handlers.c', 'sys/arch/x86/kernel/double_fault.c', 'sys/hal/time/clock_service.h', 'sys/hal/time/clock_service.c', 'sys/dev/char/rtc.h', 'sys/dev/char/rtc.c', 'sys/kern/sys.c', 'sys/dev/block/fat_diskio.c', 'tools/tests/clock_fixture_host.h', 'lib/os_time.c', 'sys/include/os_time.h', 'sys/platform/pc98/boot_platform.h', 'sys/platform/pc98/boot_platform.c', 'sys/platform/pc98/boot_display.h', 'sys/platform/pc98/boot_display.c', 'sys/platform/pc98/boot.h', 'sys/platform/pc98/boot.c', 'sys/kern/console/console_backend.h', 'sys/platform/pc98/console.c', 'sys/include/console_attr.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h', 'sys/kapi/kapi_port.c'],
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
