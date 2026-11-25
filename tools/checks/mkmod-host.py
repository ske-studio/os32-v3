#!/usr/bin/env python3
CHECK = dict(name='mkmod-host', tier=1,
    covers=['tools/ppc/module_rela.py', 'sys/include/syscall_code.h', 'sys/arch/x86/kernel/syscall_code.c', 'sys/arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'sys/platform/pc98/bootinfo_wire.h', 'sys/platform/pc98/bootinfo_native.h', 'sys/hal/time/clock_source.h', 'sys/platform/pc98/clock_source.c', 'sys/platform/pc98/pit_time.c', 'sys/platform/pc98/pit_time.h', 'sys/platform/pc98/kdebug/generated/kdebug_imports.json', 'sys/platform/pc98/kdebug/generated/kdebug_imports.h', 'sys/kern/time/ktime.c', 'sys/kern/time/time_math.c', 'sys/platform/pc98/kdebug/generated/kdebug_import_slots.c', 'sys/arch/x86/kdebug/generated/kdebug_import_stubs.S', 'sys/arch/x86/kdebug/selftest_exec.c', 'sys/kern/exec/ring3_str.h', 'tools/tests/exec_selftest_host.c', 'tools/tests/test_exec_selftest.py', 'sys/kern/exec/exec.c', 'sys/kern/exec/ring3_str.c', 'sys/kern/exec/os32x_hdr.c', 'sys/arch/x86/kernel/ring3_ls.S', 'lib/path.h', 'lib/path.c', 'sys/arch/x86/kdebug/selftest_lease.c', 'kdebug/*.c', 'sys/kern/kdebug/selftest_internal.h', 'sys/dev/hwdev/generated/hwdev_core.c', 'sys/kern/module/module_schema.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'sdk/kapi.json', 'sys/dev/net/e82557/e82557.h', 'sys/fs/vfs.h', 'sys/kern/module/module.h', 'build/kdebug.mk', 'build/module.ld', 'sys/dev/dma/dma8237.h', 'sys/platform/pc98/input/kbd.h', 'sys/dev/input/kbd_status.h', 'sys/dev/sound/cs4231/pcm_cs4231.h', 'sys/dev/char/serial.h', 'sys/dev/char/serial_plan.h', 'sys/kern/console/display_restore.h', 'sys/kern/exec/appmem.h', 'sys/kern/exec/appslot.h', 'sys/kern/exec/exec.h', 'sys/kern/exec/exec_heap.h', 'sys/kern/exec/lease.h', 'sys/fs/fd_redirect.h', 'sys/hal/gfx/gfx.h', 'sys/include/appmem_types.h', 'sys/include/bootinfo.h', 'sys/include/bootlog.h', 'sys/include/con_sink.h', 'sys/include/config.h', 'sys/include/console.h', 'sys/platform/pc98/kdebug/crt_diag.h', 'sys/include/gfx_hal.h', 'sys/include/io.h', 'sys/include/kbd.h', 'sys/include/kbd_inject.h', 'sys/include/kselftest_probe.h', 'sys/include/kprintf.h', 'sys/include/ksetjmp.h', 'sys/include/launch.h', 'sys/include/memmap.h', 'sys/platform/pc98/pc98.h', 'sys/platform/pc98/pegc.h', 'sys/include/redir_access.h', 'sys/include/sys.h', 'sys/include/text_screen.h', 'sys/include/types.h', 'sys/kapi/kapi_db.h', 'sys/hal/time/cpu_calibrate.h', 'sys/kern/time/cpu_calibrate_math.h', 'sys/platform/pc98/kdebug/crt_diag.c', 'sys/kern/mm/dma_pool.h', 'sys/kern/gui.h', 'sys/arch/x86/kernel/idt.h', 'sys/platform/pc98/pit.h', 'sys/include/kernel_tick.h', 'sys/kern/irq/irq.h', 'sys/kern/irq/irq_math.h', 'sys/kern/module/kdebug_imports.def', 'sys/kern/mm/kmalloc.h', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'sys/kern/kaudit.c', 'sys/kern/kselftest.h', 'sys/kern/kstack_hw.h', 'sys/kern/time/ktime.h', 'sys/kern/module/module_image.c', 'sys/kern/module/module_image.h', 'sys/kern/mm/paging.h', 'sys/kern/mm/pgalloc.h', 'sys/kern/mm/physmem.h', 'sys/platform/pc98/pit_math.h', 'sys/platform/pc98/sysclk.h', 'sys/kern/time/time_math.h', 'sys/platform/pc98/legacy/v86.h', 'sys/platform/pc98/legacy/v86_gcap.c', 'sys/platform/pc98/kdebug/v86_gcap.c', 'sys/platform/pc98/legacy/v86_gcap.h', 'sys/platform/pc98/kdebug/v86_gcap_diag.h', 'sys/platform/pc98/kdebug/v86_gcap_math.c', 'sys/platform/pc98/legacy/v86_gcap_math.h', 'sys/platform/pc98/legacy/v86_io.h', 'sys/platform/pc98/legacy/v86_mem.h', 'sys/platform/pc98/legacy/v86_pic.h', 'sys/kern/kstring.h', 'lib/serial_diag.h', 'sys/platform/pc98/platform_io.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_gui_shared.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/tests/host32.py', 'tools/tests/mkmod_tdd.md', 'tools/tests/module_image_host.c', 'tools/tests/module_image_tdd.md', 'sys/kern/mm/shlib.h', 'sys/kern/exec/ring3_ls.h', 'tools/tests/test_kdebug_imports.py', 'tools/tests/test_mkmod.py', 'tools/tests/test_module_image.py', 'sys/include/pci_dev_contract.h', 'sys/include/pci_bind_contract.h', 'sys/include/platform_display.h', 'sys/platform/pc98/display_private.h', 'sys/platform/pc98/display.c', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=18.4,
    kind='characterization',
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_mkmod.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kdebug_imports.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_exec_selftest.py', *(['--mutate'] if ctx.mutate else [])]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
