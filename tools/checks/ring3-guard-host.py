#!/usr/bin/env python3
CHECK = dict(name='ring3-guard-host', tier=1,
    covers=['tools/tests/host32.py', 'exec/exec.c', 'exec/ring3_str.c', 'fs/fd_redirect.c', 'kernel/gui.c', 'arch/x86/kernel/isr_handlers.c', 'sdk/kapi.json', 'tools/tests/ring3_guard_host.c', 'tools/tests/test_ring3_guard.py', 'lib/serial_diag.h', 'tools/tests/serial_diag_host.h', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h'],
    needs=[], budget=3.4)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_ring3_guard.py', '--target']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_ring3_guard.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
