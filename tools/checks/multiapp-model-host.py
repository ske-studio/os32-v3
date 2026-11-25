#!/usr/bin/env python3
CHECK = dict(name='multiapp-model-host', tier=1,
    covers=['sys/include/ksetjmp.h', 'sys/arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'tools/tests/host32.py', 'sys/kern/exec/appslot.c', 'sys/fs/fd_redirect.c', 'sys/fs/pipe_buffer.c', 'sys/kern/mm/shm.c', 'tools/tests/multiapp_impl_host.c', 'tools/tests/multiapp_model_host.c', 'tools/tests/owner_reclaim_host.c', 'tools/tests/test_multiapp_impl.py', 'tools/tests/test_multiapp_model.py', 'tools/tests/test_owner_reclaim.py', 'sys/kern/exec/launch.c', 'sys/kern/kbd_inject.c', 'sys/kern/exec/exec.c', 'sys/kern/exec/appslot.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=[], budget=4.3,
    kind='characterization',
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_multiapp_model.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_multiapp_impl.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    env['MUTATE'] = ctx.make_vars.get('MUTATE', '1' if ctx.mutate else '0')
    argv = ['python3', '-B', 'tools/tests/test_owner_reclaim.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
