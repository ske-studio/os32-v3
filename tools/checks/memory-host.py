#!/usr/bin/env python3
CHECK = dict(name='memory-host', tier=1,
    covers=['sdk/rust/os32api/src/install.rs', 'sdk/rust/os32api/src/av.rs', 'sys/hal/input/legacy_keymap.c', 'sys/hal/input/keymap.h', 'sys/input/keymaps/pc98_jp.c', 'sys/hal/input/kbd_merge.c', 'sys/arch/x86/kernel/kapi_call.c', 'sys/arch/x86/kernel/syscall_entry.c', 'tools/tests/syscall_source.py', 'sys/include/ksetjmp.h', 'sys/arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'sys/platform/pc98/boot_capture.c', 'sys/kern/mm/paging_lowmap.h', 'sys/kern/mm/paging_lowmap.inc', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/platform/pc98/irq_policy.c', 'sys/platform/pc98/platform_irq_limits.h', 'sys/hal/gfx/gfx_emergency.c', 'sys/hal/gfx/gfx_emergency.h', 'sys/arch/x86/kdebug/selftest_lease.c', 'sys/include/kselftest_probe.h', 'sys/kern/kdebug/selftest_internal.h', 'tools/tests/lease_selftest_host.h', 'tools/tests/font_guard_inactive.h', 'tools/tests/test_t3c2.py', 'tools/tests/layout_cycle_host.c', 'tools/tests/os32x_layout_host.c', 'sys/kern/exec/os32x_hdr.c', 'tools/memmap_audit_live.py', 'tools/gen_memmap.py', 'userland/lib/rt/dbgserial.c', 'userland/lib/rt/dbgserial.h', 'userland/diag/layout_cycle_test.c', 'userland/gshell/**', 'boot/loader_fat_new.asm', 'build/os32.ld', 'sys/fs/fd_redirect.c', 'sys/fs/pipe_buffer.c', 'sys/arch/x86/kernel/kentry.asm', 'sys/kern/mm/shm.c', 'sdk/link/shlib.ld', 'tools/check_artifacts.py', 'tools/mkshlib.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'tools/tests/host32.py', 'sys/kern/exec/redir_access.c', 'sys/kern/mm/kmalloc.c', 'tools/tests/lease_host.c', 'tools/tests/test_lease.py', 'tools/tests/kernel_layout.py', 'sys/kern/exec/lease.c', 'sys/kern/exec/appslot.c', 'sys/kern/exec/exec.c', 'sys/kern/exec/appmem.c', 'sys/kern/mm/shlib.c', 'sys/platform/pc98/kernel.c', 'sys/kern/mm/memory_boot.c', 'sys/platform/pc98/memory.c', 'sys/include/platform_memory.h', 'sys/platform/pc98/platform_memory_desc.h', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c', 'sys/platform/pc98/pci_resources.c', 'sys/platform/pc98/pci_resources.h', 'sys/dev/pci/pci.c', 'sys/dev/pci/pci_decode.c', 'sys/kern/mm/physmem.c', 'sys/kern/sys.c', 'sys/arch/x86/kernel/setjmp.asm', 'sys/arch/x86/kernel/tss.h', 'sys/arch/x86/kernel/isr_stub.asm', 'sys/arch/x86/kernel/ring3_entry.asm', 'sys/kern/irq/irq.c', 'sys/kern/irq/irq_math.c', 'sys/platform/pc98/legacy/v86.c', 'tools/tests/mutpar.py', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'tools/tests/app_band_pde_host.c', 'tools/tests/app_bb_overlap_host.c', 'tools/tests/device_reservation_stage_host.c', 'tools/tests/highram_stage_host.c', 'tools/tests/layout_boot_fixture.h', 'tools/tests/memory_boot_host.c', 'tools/tests/paging_rebuild_host.c', 'tools/tests/pgalloc_range_host.c', 'tools/tests/exec_budget_host.c', 'tools/tests/test_app_band_pde.py', 'tools/tests/test_app_bb_overlap.py', 'tools/tests/test_device_reservation.py', 'tools/tests/test_gfx_boot.py', 'tools/tests/gfx_boot_host.c', 'sys/hal/gfx/gfx_core.c', 'sys/platform/pc98/video/backend_pegc.c', 'sys/dev/video/cirrus/backend_cirrus.c', 'sys/platform/pc98/video/cirrus/cirrus_identify.c', 'tools/tests/test_highram_stage.py', 'tools/tests/test_ledger.py', 'tools/tests/test_exec_r1.py', 'sys/fs/vfs_stdin.h', 'sys/kern/exec/exec_stdin.inc', 'sys/kern/kbd_inject.c', 'sys/include/kbd_inject.h', 'sys/kern/console/con_sink.c', 'tools/tests/test_kbd_inject.py', 'tools/tests/kbd_inject_host.c', 'sys/kern/kdebug/selftest_kbd_inject.c', 'tools/tests/test_kstop.py', 'sys/hal/input/legacy_ring.c', 'sys/hal/input/legacy_mode.c', 'sys/hal/input/in2_int.h', 'sys/include/kbd_contract.h', 'userland/gshell/src/input.rs', 'userland/gshell/src/handler.rs', 'sys/kern/gui.c', 'sys/platform/pc98/input/kbd.c', 'tools/tests/test_memory_boot.py', 'tools/tests/test_paging_bounds.py', 'tools/tests/paging_bounds_host.c', 'tools/tests/test_pgalloc_model.py', 'tools/tests/test_pgalloc_range.py', 'tools/tests/test_physmem.py', 'tools/tests/test_exec_budget.py', 'userland/rust/gui_demo/src/lib.rs', 'userland/rust/gui_demo/Cargo.toml', 'build/app.conf', 'build/kernel.mk', 'build/programs.mk', 'sys/platform/pc98/legacy/v86_mem.h', 'sdk/allocator/**', 'sdk/allocator/nano_inputs.json', 'sdk/crt/crt0.asm', 'sdk/crt/crt0_c.c', 'sdk/crt/help.c', 'sdk/crt/syscalls.c', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sdk/include/os32/**', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link_guard.py', 'sdk/mkos32x.py', 'sdk/os32_generations.py', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/Cargo.toml', 'sdk/rust/os32api/src/cfg.rs', 'sdk/rust/os32api/src/generations.rs', 'sdk/rust/os32api/src/gui/mod.rs', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'sdk/rust/os32api/src/gui/types.rs', 'sdk/rust/os32api/src/host.rs', 'sdk/rust/os32api/src/kapi_generated.rs', 'sdk/rust/os32api/src/lib.rs', 'sdk/rust/os32api/src/ls.rs', 'sdk/rustc_stamp.py', 'tools/ci/build_cross.sh', 'userland/lib/rt/ls.c', 'userland/rust/.cargo/config.toml', 'userland/rust/Cargo.lock', 'userland/rust/Cargo.toml', 'userland/rust/libos32gui/Cargo.toml', 'userland/rust/libos32gui/src/textcore.rs', 'userland/rust/libos32gui_stub/Cargo.toml', 'userland/rust/libos32gui_stub/src/app.rs', 'userland/rust/libos32gui_stub/src/cfg.rs', 'userland/rust/libos32gui_stub/src/client.rs', 'userland/rust/libos32gui_stub/src/clip.rs', 'userland/rust/libos32gui_stub/src/draw.rs', 'userland/rust/libos32gui_stub/src/host.rs', 'userland/rust/libos32gui_stub/src/icon.rs', 'userland/rust/libos32gui_stub/src/layout.rs', 'userland/rust/libos32gui_stub/src/lib.rs', 'userland/rust/libos32gui_stub/src/modal.rs', 'userland/rust/libos32gui_stub/src/session.rs', 'userland/rust/libos32gui_stub/src/surface.rs', 'userland/rust/libos32gui_stub/src/timer.rs', 'userland/rust/libos32gui_stub/src/widget.rs', 'userland/rust/libos32gui_stub/src/window.rs', 'sys/kern/exec/owner_diag.h', 'lib/serial_diag.h', 'tools/tests/serial_diag_host.h', 'sys/hal/gfx/gfx_internal.h', 'sys/kern/exec/access_walk.c', 'sys/kern/exec/surface_query.c', 'sys/platform/pc98/video/backend_pc98.c', 'sys/hal/gfx/gfx_scroll.c', 'sys/platform/pc98/gfx_scroll.c', 'sys/platform/pc98/gfx_init.c', 'sys/platform/pc98/gfx_selector.c', 'sys/platform/pc98/gfx_surfaces.c', 'sys/kern/console/display_restore.h', 'sys/platform/pc98/video/gfx_vram.c', 'sys/hal/gfx/gfx_dirty.c', 'sys/hal/gfx/gfx_present.c', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/test_gfx_kernel_fb.py', 'sys/kern/exec/exec_heap.c', 'sys/kern/exec/exec_heap.h', 'sdk/link/app_sys.ld', 'tools/gen_memmap.py', 'sys/kern/mm/memory_boot.h', 'sys/kern/init/kaudit_boot.c', 'sys/kern/module/module.h', 'userland/lib/font/**', 'sys/arch/x86/kernel/cpu_flags.inc', 'sys/hal/input/input_service.h', 'sys/include/kbd.h', 'sys/platform/pc98/input/kbd.h', 'sys/hal/input/input_service.c', 'sys/hal/input/pointer_service.h', 'sys/dev/pci/pci_bind.h', 'sys/kern/irq/irq.h', 'sys/kern/irq/irq_math.h', 'sys/platform/pc98/boot_platform.h', 'sys/platform/pc98/boot.c', 'sys/include/pci_dev_contract.h', 'sys/include/pci_bind_contract.h', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/include/kapi_call.h', 'sys/include/kapi_call_generated.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=79.1,
    basis=['AC-MEM-1'],
    env_limit='hostでメモリ管理の境界を確認するが、IRQ・CR3・TLB・RAMが模型のため実CPUの写像切替と競合は保証しない。',
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_physmem.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_paging_bounds.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_paging_bounds.py', '--rebuild', 'nonmaster']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_paging_bounds.py', '--rebuild', 'rollback']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_app_band_pde.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_pgalloc_model.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_pgalloc_range.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_highram_stage.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_memory_boot.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_ledger.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_lease.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_exec_r1.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_t3c2.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kstop.py', 'sys/hal/input/legacy_ring.c', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_device_reservation.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_exec_budget.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_app_bb_overlap.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_gfx_boot.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
