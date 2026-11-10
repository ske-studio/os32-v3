#!/usr/bin/env python3
CHECK = dict(name='p2v', tier=1,
    covers=['platform/pc98/gfx_scroll.c', 'platform/pc98/gfx_init.c', 'platform/pc98/gfx_selector.c', 'platform/pc98/gfx_surfaces.c', 'arch/x86/legacy/*.[ch]', 'arch/x86/legacy/*.inc', 'platform/pc98/legacy/*.[ch]', 'platform/pc98/legacy/*.inc', 'platform/pc98/bios_snapshot.h', 'platform/pc98/bootinfo_wire.h', 'platform/pc98/bootinfo_native.h', 'platform/pc98/boot_capture.c', 'platform/pc98/bios_snapshot.c', 'platform/pc98/bootinfo_report.c', 'platform/pc98/bootinfo_geom.c', 'platform/pc98/bootinfo_check.c', 'platform/pc98/bootinfo_capture.c', 'tools/clang_ast/result_cache.py', 'kernel/module/kdebug_imports.def', 'tools/source_roots.py', 'tools/prototypes/geforce_imports.def', 'tools/prototypes/v86_imports.def', 'tools/clang_ast/**', 'tools/gen_hwdev_pair.py', 'tools/gen_sqlite_imports.py', 'tools/prototypes/cd_imports.def', 'tools/prototypes/pcm_imports.def', 'tools/prototypes/lan_imports.def', 'tools/prototypes/cirrus_imports.def', 'tools/prototypes/trident_imports.def', 'tools/tests/test_clang_ast.py', 'tools/check_p2v.py', 'tools/check_c_dialect.py', 'tools/check_arch_asm.py', 'tools/check_p2v_allow.txt', 'tools/tests/test_p2v.py', 'tools/tests/mutpar.py', 'kernel/**/*.[ch]', 'kernel/**/*.inc', 'arch/x86/legacy/*.asm', 'init/**/*.[ch]', 'init/**/*.inc', 'mm/**/*.[ch]', 'mm/**/*.inc', 'drivers/**/*.[ch]', 'drivers/**/*.inc', 'drivers/**/*.asm', 'gfx/**/*.[ch]', 'gfx/**/*.inc', 'fs/**/*.[ch]', 'fs/**/*.inc', 'exec/**/*.[ch]', 'exec/**/*.inc', 'kapi/**/*.[ch]', 'kapi/**/*.inc', 'lib/**/*.[ch]', 'lib/**/*.inc', 'lib/**/*.asm', 'include/**/*.[ch]', 'kdebug/**/*.[ch]', 'kdebug/**/*.inc', 'kdebug/**/*.asm', 'tools/gen_memmap.py', 'arch/x86/kernel/kentry.asm', 'boot/loader_fat_new.asm', 'build/os32.ld', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/mkshlib.py', 'tools/module_schema.py', 'tools/prototypes/sqlite_imports.def', 'tools/tests/kernel_includes.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'boot/boot_main.c', 'sdk/allocator/nano_adapter.c', 'sdk/crt/syscalls.c', 'tools/check_le_access.py', 'userland/lib/rt/ls.c', 'userland/tests/sqlite_standalone/sqlite_standalone.c', 'userland/tests/sqlite_standalone/sqlite_user_vfs.c', 'arch/x86/kernel/**/*.[ch]', 'arch/x86/video/*.[ch]', 'platform/pc98/ime_render_tvram.c', 'platform/pc98/ime_display.c'],
    needs=['make:all'], budget=484.0)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/check_p2v.py']
    if ctx.files is not None:
        argv += ['--files', *ctx.files]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', 'tools/tests/test_p2v.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
