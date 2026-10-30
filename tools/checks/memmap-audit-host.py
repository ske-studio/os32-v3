#!/usr/bin/env python3
CHECK = dict(name='memmap-audit-host', tier=3,
    covers=['kdebug/selftest_lease.c', 'include/kselftest_probe.h', 'kdebug/selftest_internal.h', 'tools/tests/lease_selftest_host.h', 'tools/tests/font_guard_inactive.h', 'kernel/v86/v86_snapshot.c', 'arch/x86/**', 'exec/access_walk.c', 'exec/exec.c', 'exec/exec_image.inc', 'exec/lease.c', 'exec/redir_access.c', 'exec/surface_query.c', 'exec/system_surface.*', 'gfx/gfx.h', 'drivers/video/cirrus/backend_cirrus.c', 'drivers/video/cirrus/cirrus_identify.c', 'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pegc.c', 'gfx/gfx_core.c', 'gfx/gfx_scroll.c', 'gfx/gfx_vram.c', 'include/**', 'init/kernel.c', 'kernel/kaudit.c', 'kernel/kselftest.h', 'kernel/kselftest_report.c', 'kernel/module/module.h', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'mm/paging.c', 'mm/paging.h', 'mm/pgalloc.c', 'mm/physmem.c', 'arch/x86/kernel/ring3_entry.asm', 'arch/x86/kernel/iret_user.inc', 'mm/shlib.c', 'mm/shlib.h', 'kernel/sys.c', 'kernel/v86/v86_gcap.*', 'kdebug/v86_gcap.*', 'kernel/v86/v86_gcap_math.h', 'kernel/v86/v86_mem.*', 'platform/pc98/**', 'sdk/include/**', 'tools/tests/e10c_gcap_host.c', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/gfx_reinit_host.c', 'tools/tests/host32.py', 'tools/tests/host_arch/**', 'tools/tests/memmap_audit_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_e10c_lifecycle.py', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/kernel_layout.py', 'tools/tests/test_gfx_reinit.py', 'tools/tests/test_memmap_audit.py', 'tools/tests/test_shm_user.py', 'exec/exec_kapi_init.inc', 'mm/shm.c', 'kernel/v86/v86.c', 'kernel/v86/v86_gp_emulate.inc', 'kernel/v86/v86_bios.c', 'tools/tests/shm_user_host.c', 'tools/tests/serial_diag_host.h', 'lib/serial_diag.h', 'arch/x86/kernel/cpu_flags.inc', 'arch/x86/kernel/entry_layout.inc', 'kernel/console/display_restore.h', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc'],
    needs=['make:all'], budget=7.522638)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_memmap_audit.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_e10c_lifecycle.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
