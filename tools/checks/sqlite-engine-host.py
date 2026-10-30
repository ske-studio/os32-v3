#!/usr/bin/env python3
CHECK = dict(name='sqlite-engine-host', tier=1,
    covers=['tools/tests/font_guard_inactive.h', 'tools/gen_sqlite_imports.py', 'tools/prototypes/sqlite_imports.def', 'lib/sqlite3/generated/*', 'kernel/sqlite_engine.c', 'kernel/sqlite_engine.h', 'kernel/sqlite_outer.c', 'lib/sqlite3/os32_sqlite_outer.h', 'lib/sqlite3/sqlite_stack.asm', 'lib/sqlite3/os32_sqlite_test.c', 'exec/exec.c', 'exec/exec_image.inc', 'arch/x86/kernel/isr_handlers.c', 'init/kernel.c', 'kapi/kapi_db.c', 'kernel/ime/ime_dict.c', 'tools/tests/test_sqlite_engine.py', 'tools/tests/sqlite_engine_host.c', 'tools/tests/sqlite_engine_regs.asm', 'tools/tests/host32.py', 'exec/exec_kapi_init.inc', 'kernel/ime/ime.h', 'kernel/ime/ime_render.h', 'kernel/console/unicode_file.h', 'tools/tests/kernel_includes.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc', 'kernel/module/kdebug_imports.def', 'lib/sqlite3/os32_sqlite_vfs.c', 'sdk/kapi.json', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/module_schema.py'],
    needs=['make:all'], budget=2.6782)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_sqlite_engine.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
