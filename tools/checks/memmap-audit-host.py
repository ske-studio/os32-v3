#!/usr/bin/env python3
CHECK = dict(name='memmap-audit-host', tier=3,
    covers=['sys/include/display_provider.h', 'sys/platform/pc98/bios_snapshot.h', 'sys/platform/pc98/boot_capture.c', 'sys/kern/mm/paging_lowmap.h', 'sys/kern/mm/paging_lowmap.inc', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/hal/gfx/gfx_emergency.c', 'sys/hal/gfx/gfx_emergency.h', 'sys/arch/x86/kdebug/selftest_lease.c', 'sys/include/kselftest_probe.h', 'sys/kern/kdebug/selftest_internal.h', 'tools/tests/lease_selftest_host.h', 'tools/tests/font_guard_inactive.h', 'sys/platform/pc98/bios_snapshot.c', 'arch/x86/**', 'sys/kern/exec/access_walk.c', 'sys/kern/exec/exec.c', 'sys/kern/exec/lease.c', 'sys/kern/exec/redir_access.c', 'sys/kern/exec/surface_query.c', 'exec/system_surface.*', 'sys/hal/gfx/gfx.h', 'sys/dev/video/cirrus/backend_cirrus.c', 'sys/platform/pc98/video/cirrus/cirrus_identify.c', 'sys/platform/pc98/video/backend_pc98.c', 'sys/platform/pc98/video/backend_pegc.c', 'sys/hal/gfx/gfx_core.c', 'sys/hal/gfx/gfx_scroll.c', 'sys/platform/pc98/gfx_scroll.c', 'sys/platform/pc98/gfx_init.c', 'sys/platform/pc98/gfx_selector.c', 'sys/platform/pc98/gfx_surfaces.c', 'sys/platform/pc98/video/gfx_vram.c', 'sys/hal/gfx/gfx_dirty.c', 'sys/hal/gfx/gfx_present.c', 'include/**', 'sys/platform/pc98/kernel.c', 'sys/kern/kaudit.c', 'sys/kern/kselftest.h', 'sys/kern/kselftest_report.c', 'sys/kern/module/module.h', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'sys/kern/mm/paging.c', 'sys/kern/mm/paging.h', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/pgalloc_owner.inc', 'sys/kern/mm/physmem.c', 'sys/arch/x86/kernel/ring3_entry.asm', 'sys/arch/x86/kernel/iret_user.inc', 'sys/kern/mm/shlib.c', 'sys/kern/mm/shlib.h', 'sys/kern/sys.c', 'platform/pc98/legacy/v86_gcap.*', 'kdebug/v86_gcap.*', 'sys/platform/pc98/legacy/v86_gcap_math.h', 'platform/pc98/legacy/v86_mem.*', 'platform/pc98/**', 'sdk/include/**', 'tools/tests/e10c_gcap_host.c', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/gfx_reinit_host.c', 'tools/tests/host32.py', 'tools/tests/host_arch/**', 'tools/tests/memmap_audit_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_e10c_lifecycle.py', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/kernel_layout.py', 'tools/tests/test_gfx_reinit.py', 'tools/tests/test_memmap_audit.py', 'tools/tests/test_shm_user.py', 'sys/kern/mm/shm.c', 'sys/platform/pc98/legacy/v86.c', 'sys/platform/pc98/legacy/v86_bios.c', 'tools/tests/shm_user_host.c', 'tools/tests/serial_diag_host.h', 'lib/serial_diag.h', 'sys/kern/console/display_restore.h', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=10.1,
    basis=['HAL_V2_DESIGN §13-2'],
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
