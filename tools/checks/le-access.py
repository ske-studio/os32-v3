#!/usr/bin/env python3
CHECK = dict(name='le-access', tier=1,
    covers=['fs/ext2/ext2_fmt_*.inc', 'tools/clang_ast/**', 'tools/gen_hwdev_pair.py', 'tools/gen_sqlite_imports.py', 'tools/prototypes/cd_imports.def', 'tools/prototypes/pcm_imports.def', 'tools/prototypes/lan_imports.def', 'tools/prototypes/cirrus_imports.def', 'tools/tests/test_clang_ast.py', 'drivers/**/*.[ch]', 'fs/**/*.[ch]', 'lib/**/*.[ch]', 'kernel/**/*.[ch]', 'include/**', 'tools/check_arm_compile.py', 'tools/check_le_access.py', 'arch/x86/kernel/kentry.asm', 'boot/boot_main.c', 'boot/loader_fat_new.asm', 'build/kernel.mk', 'build/os32.ld', 'init/kernel.c', 'kernel/module/kdebug_imports.def', 'mm/shm.c', 'sdk/allocator/nano_adapter.c', 'sdk/crt/syscalls.c', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_arch_asm.py', 'tools/check_artifacts.py', 'tools/check_c_dialect.py', 'tools/check_p2v.py', 'tools/check_p2v_allow.txt', 'tools/gen_kdebug_imports.py', 'tools/gen_memmap.py', 'tools/mkmod.py', 'tools/mkshlib.py', 'tools/module_schema.py', 'tools/prototypes/geforce_imports.def', 'tools/prototypes/sqlite_imports.def', 'tools/source_roots.py', 'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/lib/rt/ls.c', 'userland/rust/libos32gui/src/shlib.rs', 'userland/tests/sqlite_standalone/sqlite_standalone.c', 'userland/tests/sqlite_standalone/sqlite_user_vfs.c'],
    needs=['make:all'], budget=5.442474)


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
