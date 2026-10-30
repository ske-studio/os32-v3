#!/usr/bin/env python3
CHECK = dict(name='guest-acceptance-host', tier=1,
    covers=['tools/lanfs_host.py', 'tools/lan_bridge.py', 'tools/tests/guest_acceptance.yaml', 'tools/tests/guest_tests.txt', 'tools/gen_guest_acceptance.py', 'tools/accept/pt0_snapshot.py', 'tools/accept/as_extents_at_teardown.py', 'exec/exec.c', 'exec/appslot.c', 'exec/exec_kapi_init.inc', 'exec/appslot.h', 'mm/paging.h', 'include/appmem_types.h', 'tools/tvdump_recv.py', 'tools/tests/test_guest_acceptance.py', 'tools/tests/mutpar.py', 'tools/guest_tests.py', 'tools/np21w_mcp/np21w_client.py', 'tools/np21w_mcp/symbols.py', 'include/memmap.h', 'userland/tests/ring3_marker.h', 'docs/09_exec.md', 'docs/KAPI_SPEC.md', 'docs/decisions/MEMMAP_V3.md', 'docs/08_build.md', 'docs/tasks/DEFERRED_TESTS.md', 'build/core.yaml', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/hostdrv_deploy.py', 'userland/deploy.yaml', 'tools/rshell_serial.py', 'tools/rshell_peek.py', 'tools/serialfs_host.py', 'docs/POLICY_DEBUG.md', 'tools/tests/kernel_includes.py', 'exec/exec_image.inc'],
    needs=['make:all'], budget=7.15214)


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
