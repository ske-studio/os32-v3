#!/usr/bin/env python3
CHECK = dict(name='exec-image-host', tier=1,
    covers=['arch/x86/exec_shlib.h', 'arch/ppc/exec_shlib.h', 'arch/ppc/exec_shlib.c', 'arch/x86/exec_reclaim.h', 'arch/ppc/exec_reclaim.h', 'arch/ppc/exec_reclaim.c', 'include/ksetjmp.h', 'arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'include/callconv.h', 'include/abi.h', 'arch/x86/arch_abi.h', 'exec/os32x_hdr.c', 'exec/os32x_hdr.h', 'tools/tests/os32x_layout_host.c', 'sdk/include/os32/os32_kapi_shared.h', 'exec/exec.c', 'tools/tests/test_exec_image.py', 'tools/tests/exec_image_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h'],
    needs=[], budget=4.8)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_exec_image.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
