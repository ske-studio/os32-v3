#!/usr/bin/env python3
CHECK = dict(name='sqlite-engine-host', tier=1,
    covers=['sys/arch/x86/arch_kernel_fp.h', 'tools/tests/host_arch/arch_kernel_fp.h', 'sys/include/diag_stack.h', 'arch/*/arch_diag_stack.h', 'tools/tests/font_guard_inactive.h', 'tools/gen_sqlite_imports.py', 'tools/prototypes/sqlite_imports.def', 'lib/sqlite3/generated/*', 'sys/kern/sqlite_engine.c', 'sys/kern/sqlite_engine.h', 'sys/kern/sqlite_engine_body.h', 'sys/kern/sqlite_outer.c', 'lib/sqlite3/os32_sqlite_outer.h', 'lib/sqlite3/sqlite_stack.asm', 'lib/sqlite3/os32_sqlite_test.c', 'sys/kern/exec/exec.c', 'sys/arch/x86/kernel/isr_handlers.c', 'sys/platform/pc98/kernel.c', 'sys/kapi/kapi_db.c', 'sys/kern/ime/ime_dict.c', 'tools/tests/test_sqlite_engine.py', 'tools/tests/sqlite_engine_host.c', 'tools/tests/sqlite_engine_regs.asm', 'tools/tests/host32.py', 'sys/kern/ime/ime.h', 'sys/include/ime_boot.h', 'sys/kern/ime/ime_render.h', 'sys/kern/console/unicode_file.h', 'tools/tests/kernel_includes.py', 'sys/kern/module/kdebug_imports.def', 'lib/sqlite3/os32_sqlite_vfs.c', 'sdk/kapi.json', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/module_schema.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=3.3,
    kind='characterization',
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
        argv = ['python3', '-B', 'tools/tests/test_sqlite_engine.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
