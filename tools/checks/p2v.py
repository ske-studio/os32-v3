#!/usr/bin/env python3
CHECK = dict(name='p2v', tier=1,
    covers=['tools/source_roots.py', 'tools/prototypes/geforce_imports.def', 'tools/prototypes/v86_imports.def', 'tools/clang_ast/**', 'tools/gen_hwdev_pair.py', 'tools/gen_sqlite_imports.py', 'tools/prototypes/cd_imports.def', 'tools/prototypes/pcm_imports.def', 'tools/prototypes/lan_imports.def', 'tools/prototypes/cirrus_imports.def', 'tools/tests/test_clang_ast.py', 'tools/check_p2v.py', 'tools/check_c_dialect.py', 'tools/check_arch_asm.py', 'tools/check_p2v_allow.txt', 'tools/tests/test_p2v.py', 'tools/tests/mutpar.py', 'kernel/**', 'init/**', 'mm/**', 'drivers/**', 'gfx/**', 'fs/**', 'exec/**', 'kapi/**', 'lib/**', 'include/**', 'kdebug/**', 'tools/gen_memmap.py', 'arch/x86/kernel/kentry.asm', 'boot/loader_fat_new.asm', 'build/os32.ld', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/mkshlib.py', 'tools/module_schema.py', 'tools/prototypes/sqlite_imports.def', 'tools/tests/kernel_includes.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'boot/boot_main.c', 'sdk/allocator/nano_adapter.c', 'sdk/crt/syscalls.c', 'tools/check_le_access.py', 'userland/lib/rt/ls.c', 'userland/tests/sqlite_standalone/sqlite_standalone.c', 'userland/tests/sqlite_standalone/sqlite_user_vfs.c'],
    needs=['make:all'], budget=215.213702)


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
