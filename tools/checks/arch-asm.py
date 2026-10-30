#!/usr/bin/env python3
CHECK = dict(name='arch-asm', tier=1,
    covers=['tools/source_roots.py', 'tools/prototypes/geforce_imports.def', 'tools/prototypes/v86_imports.def', 'tools/check_le_access.py', 'tools/check_p2v.py', 'tools/check_p2v_allow.txt', 'tools/tests/mutpar.py', 'tools/clang_ast/**', 'tools/gen_hwdev_pair.py', 'tools/gen_sqlite_imports.py', 'tools/prototypes/cd_imports.def', 'tools/prototypes/pcm_imports.def', 'tools/prototypes/lan_imports.def', 'tools/prototypes/cirrus_imports.def', 'tools/tests/test_clang_ast.py', 'tools/tests/test_p2v.py', 'tools/check_c_dialect.py', 'boot/boot_main.c', 'sdk/allocator/nano_adapter.c', 'sdk/crt/syscalls.c', 'userland/lib/rt/ls.c', 'kernel/**', 'init/**', 'mm/**', 'drivers/**', 'exec/**', 'fs/**', 'kapi/**', 'lib/**', 'net/**', 'gfx/**', 'include/**', 'arch/**', 'platform/**', 'tools/check_arch_asm.py', 'tools/check_map.d/check-arch-asm.yaml', 'build/checks.d/check-arch-asm.mk', 'kdebug/**', 'tools/gen_memmap.py', 'tools/clang_ast/__init__.py', 'boot/bootinfo.inc', 'boot/loader_fat_new.asm', 'build/os32.ld', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/mkshlib.py', 'tools/module_schema.py', 'tools/prototypes/sqlite_imports.def', 'tools/tests/kernel_includes.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'userland/tests/sqlite_standalone/sqlite_standalone.c', 'userland/tests/sqlite_standalone/sqlite_user_vfs.c'],
    needs=['make:all'], budget=138.833408)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/check_arch_asm.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', 'tools/tests/test_clang_ast.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
