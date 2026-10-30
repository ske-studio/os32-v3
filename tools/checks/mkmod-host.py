#!/usr/bin/env python3
CHECK = dict(name='mkmod-host', tier=1,
    covers=['exec/exec_kapi_init.inc', 'kdebug/generated/kdebug_import_slots.c', 'kdebug/generated/kdebug_import_stubs.S', 'kdebug/selftest_exec.c', 'exec/ring3_str.h', 'tools/tests/exec_selftest_host.c', 'tools/tests/test_exec_selftest.py', 'exec/exec.c', 'exec/exec_image.inc', 'exec/ring3_str.c', 'exec/os32x_hdr.c', 'exec/ring3_ls.S', 'lib/path.h', 'kdebug/selftest_lease.c', 'kdebug/*.c', 'kdebug/selftest_internal.h', 'hwdev/generated/hwdev_core.c', 'kernel/module/module_schema.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'sdk/kapi.json', 'drivers/net/e82557/e82557.h', 'fs/vfs.h', 'kernel/module/module.h', 'build/checks.d/check-mkmod-host.mk', 'build/kdebug.mk', 'build/module.ld', 'drivers/dma/dma8237.h', 'drivers/input/kbd.h', 'drivers/input/kbd_status.h', 'drivers/sound/cs4231/pcm_cs4231.h', 'drivers/char/serial.h', 'drivers/char/serial_plan.h', 'kernel/console/display_restore.h', 'exec/appmem.h', 'exec/appslot.h', 'exec/exec.h', 'exec/exec_heap.h', 'exec/lease.h', 'fs/fd_redirect.h', 'gfx/gfx.h', 'include/appmem_types.h', 'include/bootinfo.h', 'include/bootlog.h', 'include/con_sink.h', 'include/config.h', 'include/console.h', 'include/crt_diag.h', 'include/gfx_hal.h', 'include/io.h', 'include/kbd.h', 'include/kbd_inject.h', 'include/kselftest_probe.h', 'include/kprintf.h', 'include/ksetjmp.h', 'include/launch.h', 'include/memmap.h', 'include/pc98.h', 'include/pegc.h', 'include/redir_access.h', 'include/sys.h', 'include/tvram.h', 'include/types.h', 'kapi/kapi_db.h', 'kernel/time/cpu_calibrate.h', 'kernel/time/cpu_calibrate_math.h', 'kdebug/crt_diag.c', 'mm/dma_pool.h', 'kernel/gui.h', 'arch/x86/kernel/idt.h', 'kernel/irq/irq.h', 'kernel/irq/irq_math.h', 'kernel/module/kdebug_imports.def', 'mm/kmalloc.h', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'kernel/kaudit.c', 'kernel/kselftest.h', 'kernel/kstack_hw.h', 'kernel/time/ktime.h', 'kernel/module/module_image.c', 'kernel/module/module_image.h', 'mm/paging.h', 'mm/pgalloc.h', 'mm/physmem.h', 'kernel/time/pit_math.h', 'kernel/time/sysclk.h', 'kernel/time/time_math.h', 'kernel/v86/v86.h', 'kernel/v86/v86_gcap.c', 'kdebug/v86_gcap.c', 'kernel/v86/v86_gcap.h', 'kdebug/v86_gcap_diag.h', 'kdebug/v86_gcap_math.c', 'kernel/v86/v86_gcap_math.h', 'kernel/v86/v86_io.h', 'kernel/v86/v86_mem.h', 'kernel/v86/v86_pic.h', 'lib/crc32_core.inc', 'lib/kstring.h', 'lib/serial_diag.h', 'platform/pc98/platform_io.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_gui_shared.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'tools/check_map.d/check-mkmod-host.yaml', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/tests/host32.py', 'tools/tests/mkmod_tdd.md', 'tools/tests/module_image_host.c', 'tools/tests/module_image_tdd.md', 'mm/shlib.h', 'exec/ring3_ls.h', 'tools/tests/test_kdebug_imports.py', 'tools/tests/test_mkmod.py', 'tools/tests/test_module_image.py', 'include/pci_dev_contract.h', 'include/pci_bind_contract.h', 'include/platform_display.h', 'platform/pc98/display_private.h', 'platform/pc98/display.c', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=10.899528)


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
    argv = ['python3', '-B', 'tools/tests/test_exec_selftest.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
