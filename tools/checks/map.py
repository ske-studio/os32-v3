#!/usr/bin/env python3
CHECK = dict(name='map', tier=1,
    covers=['tools/checks/**/*.py', 'tools/tests/test_*.py', 'tools/gen_hwdev_pair.py', 'CLAUDE.md', 'README.md', 'exec/exec_kapi_init.inc', 'sdk/kapi.json', 'sdk/rust/os32api/src/kapi_generated.rs', 'boot/boot_main.c', 'init/kernel.c', 'kernel/time/sysclk.c', 'lib/sqlite3/os32_sqlite_test.c', 'lib/sqlite3/os32_sqlite_vfs.c', 'lib/sqlite3/sqlite3.c', 'sdk/example/hello/Makefile', 'tools/check_c_dialect.py', 'tools/clang_ast/build.py', 'tools/clang_ast/dialect.py', 'tools/tests/mutpar.py', 'tools/tests/test_c_dialect.py', 'userland/tests/sqlite_standalone/sqlite_standalone.c', 'userland/tests/sqlite_standalone/sqlite_user_vfs.c', 'sdk/gen_kapi.py', 'build/kernel.mk', 'build/libs.mk', 'build/os32.ld', 'build/programs.mk', 'kapi/kapi_generated.c', 'sdk/crt/generations.inc', 'sdk/kapi_rust_gen.py', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/generations.ld', 'sdk/link/shlib.ld', 'sdk/os32_generations.py', 'sdk/rust/os32api/src/generations.rs', 'tools/pack_name_tables.txt', 'tools/pack_notes.txt', 'boot/loader_fat_new.asm', 'fs/fd_redirect.c', 'fs/pipe_buffer.c', 'arch/x86/kernel/kentry.asm', 'mm/shm.c', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/gen_memmap.py', 'tools/mkshlib.py', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'kernel/module/kdebug_imports.def', 'tools/gen_kdebug_imports.py', 'tools/gen_sqlite_imports.py', 'tools/mkmod.py', 'tools/module_schema.py', 'tools/prototypes/sqlite_imports.def', 'tools/prototypes/fep_imports.def', 'tools/prototypes/cd_imports.def', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=99.015516)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/checks/run.py', '--lint']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
