#!/usr/bin/env python3
CHECK = dict(name='le-access', tier=1,
    covers=['sys/dev/hwdev/hwdev_module.c', 'sys/platform/pc98/sysclk.c', 'sys/platform/pc98/boot_display.c', 'sys/platform/pc98/boot_platform.c', 'sys/platform/pc98/boot_splash.c', 'sys/platform/powermac/ppc_kernel.c', 'sys/platform/pc98/gfx_scroll.c', 'sys/platform/pc98/gfx_init.c', 'sys/platform/pc98/gfx_selector.c', 'sys/platform/pc98/gfx_surfaces.c', 'sys/platform/pc98/boot_capture.c', 'sys/platform/pc98/bios_snapshot.c', 'sys/platform/pc98/bootinfo_report.c', 'sys/platform/pc98/bootinfo_geom.c', 'sys/platform/pc98/bootinfo_check.c', 'sys/platform/pc98/bootinfo_capture.c', 'fs/ext2/ext2_fmt_*.inc', 'tools/clang_ast/**', 'tools/gen_hwdev_pair.py', 'tools/gen_sqlite_imports.py', 'tools/prototypes/cd_imports.def', 'tools/prototypes/pcm_imports.def', 'tools/prototypes/lan_imports.def', 'tools/prototypes/cirrus_imports.def', 'tools/tests/test_clang_ast.py', 'drivers/**/*.[ch]', 'fs/**/*.[ch]', 'lib/**/*.[ch]', 'kernel/**/*.[ch]', 'include/**', 'tools/check_arm_compile.py', 'tools/check_le_access.py', 'sys/arch/x86/kernel/kentry.asm', 'boot/boot_main.c', 'boot/loader_fat_new.asm', 'build/kernel.mk', 'build/os32.ld', 'sys/platform/pc98/kernel.c', 'sys/kern/module/kdebug_imports.def', 'sys/kern/mm/shm.c', 'sdk/allocator/nano_adapter.c', 'sdk/crt/syscalls.c', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_arch_asm.py', 'tools/check_artifacts.py', 'tools/check_c_dialect.py', 'tools/check_p2v.py', 'tools/check_p2v_allow.txt', 'tools/gen_kdebug_imports.py', 'tools/gen_memmap.py', 'tools/mkmod.py', 'tools/mkshlib.py', 'tools/module_schema.py', 'tools/prototypes/usb_imports.def', 'tools/prototypes/geforce_imports.def', 'tools/prototypes/sqlite_imports.def', 'tools/source_roots.py', 'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/lib/rt/ls.c', 'userland/rust/libos32gui/src/shlib.rs', 'userland/diag/sqlite_standalone/sqlite_standalone.c', 'userland/diag/sqlite_standalone/sqlite_user_vfs.c', 'sys/platform/pc98/ime_display.c', 'sys/platform/pc98/ime_render_tvram.c'],
    needs=['make:all'], budget=8.5,
    kind='characterization',
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/check_le_access.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
