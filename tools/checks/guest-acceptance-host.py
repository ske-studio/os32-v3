#!/usr/bin/env python3
CHECK = dict(name='guest-acceptance-host', tier=1,
    covers=['sys/include/ksetjmp.h', 'sys/arch/x86/arch_jmp_storage.h', 'tools/tests/host_arch/arch_jmp_storage.h', 'tools/lanfs_host.py', 'tools/lan_bridge.py', 'tools/host_image.py', 'tools/guest_acceptance.yaml', 'tools/guest_tests.txt', 'tools/gen_guest_acceptance.py', 'tools/ops/pt0_snapshot.py', 'tools/ops/as_extents_at_teardown.py', 'sys/kern/exec/exec.c', 'sys/kern/exec/appslot.c', 'sys/kern/exec/appslot.h', 'sys/kern/mm/paging.h', 'sys/include/appmem_types.h', 'tools/tvdump_recv.py', 'tools/tests/test_guest_acceptance.py', 'tools/tests/mutpar.py', 'tools/guest_tests.py', 'tools/np21w_mcp/np21w_client.py', 'tools/np21w_mcp/symbols.py', 'sys/include/memmap.h', 'userland/diag/ring3_marker.h', 'docs/09_exec.md', 'docs/KAPI_SPEC.md', 'docs/design/include/memmap.md', 'docs/design/include/gfx_mode.md', 'sdk/include/os32/os32_generations.h', 'docs/08_build.md', 'docs/tasks/DEFERRED_TESTS.md', 'build/core.yaml', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/hostdrv_deploy.py', 'userland/deploy.yaml', 'tools/rshell_serial.py', 'tools/rshell_peek.py', 'tools/rshell_hwb.py', 'tools/serialfs_host.py', 'tools/host_tree_manifest.py', 'docs/POLICY_DEBUG.md', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=10.0,
    basis=['[D1]', '[D2]'],
)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/gen_guest_acceptance.py', '--check']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_guest_acceptance.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
