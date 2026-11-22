CHECK = dict(name='ppc-kapi-v89-guest', tier=2, needs=[], budget=1200,
    env=['ppc-cross', 'qemu-ppc', 'socket'], full_only=True,
    covers=['tools/checks/ppc-kapi-v89-guest.py', 'tools/accept/ppc_kapi_v89.py', 'tools/accept/ppc_shm.py',
            'tools/ppc/**', 'tools/*.py', 'userland/ppc/**', 'userland/shell/**',
            'Makefile', 'build/**', 'sdk/**', 'mm/**', 'exec/**', 'arch/ppc/**',
            'platform/powermac/**', 'include/**', 'init/**', 'kernel/**',
            'kapi/**', 'lib/**', 'fs/**', 'drivers/**', 'input/**', 'net/**',
            'gfx/**', 'assets/fonts/**'],
    basis=['docs/decisions/KAPI_CAPACITY_V89_DESIGN.md §1, §2, §4 items 4–5',
           'docs/decisions/KAPI_CAPACITY_V89_DESIGN.md §2: 11-21 PM/mainline reading: both ranges prepared before USER PTE publication; failure stops before user entry',
           'sdk/kapi.json: slots 0 gfx_init and 297 io_pump; last published slot at api length - 1, reserved through func_capacity - 1',
           'kernel/io_kapi.c: io_pump(0) returns OS32_ERR_INVAL (-9)',
           'exec/exec_syscall.inc: first and last reserved guest slots: child fault, shell status 139', 'exec/exec_syscall.inc: slot >= KAPI_FUNC_COUNT',
           'userland/shell/sh_exec.inc: run_cmd_report', 'userland/shell/main.c: EXEC_KIND_FAULT'],
    env_limit='Requires mac99 QEMU and loopback UDP/TCP for link-sh RSH.')


def run(ctx):
    import os
    import subprocess

    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    rc = subprocess.call(['python3', '-B', 'tools/accept/ppc_kapi_v89.py'] +
                         (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
    # rc 3 = toolchain/QEMU/socket absent; only a failure when the PPC environment is required.
    return 0 if rc == 3 and os.environ.get('OS32_REQUIRE_PPC') != '1' else rc
