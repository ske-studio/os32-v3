#!/usr/bin/env python3
CHECK = dict(name='c-dialect', tier=1,
    covers=['tools/prototypes/geforce_imports.def', 'tools/prototypes/v86_imports.def', 'tools/clang_ast/**', 'tools/gen_hwdev_pair.py', 'tools/tests/test_clang_ast.py', 'tools/tests/test_p2v.py', '**/*.[ch]', '**/*.inc', 'sdk/example/hello/**', 'tools/check_c_dialect.py', 'tools/gen_sqlite_imports.py', 'tools/gen_kdebug_imports.py', 'tools/prototypes/sqlite_imports.def', 'tools/prototypes/fep_imports.def', 'tools/prototypes/cd_imports.def', 'tools/prototypes/pcm_imports.def', 'tools/prototypes/lan_imports.def', 'tools/prototypes/cirrus_imports.def', 'build/sqlite-proto.mk', 'tools/gen_memmap.py', 'boot/loader_fat_new.asm', 'build/os32.ld', 'arch/x86/kernel/kentry.asm', 'sdk/kapi.json', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/gui/proto.rs', 'sdk/rust/os32api/src/gui/stub.rs', 'tools/check_artifacts.py', 'tools/mkshlib.py', 'tools/tests/test_owner_reclaim.py', 'userland/rust/libos32gui/src/shlib.rs', 'kernel/module/kdebug_imports.def', 'tools/mkmod.py', 'tools/module_schema.py', 'tools/tests/kernel_includes.py', 'tools/check_arch_asm.py', 'tools/check_le_access.py', 'tools/check_p2v.py', 'tools/check_p2v_allow.txt', 'tools/source_roots.py', 'tools/tests/mutpar.py'],
    needs=['make:all'], budget=308.73848)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/check_c_dialect.py']
    if ctx.files is not None:
        argv += ['--files', *ctx.files]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
