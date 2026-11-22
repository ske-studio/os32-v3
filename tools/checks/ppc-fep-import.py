CHECK = dict(name='ppc-fep-import', tier=2, needs=[], budget=1200,
    env=['ppc-cross', 'qemu-ppc', 'socket'], full_only=True,
    covers=['tools/checks/ppc-fep-import.py', 'tools/accept/ppc_fep.py', 'tools/accept/ppc_module.py', 'tools/accept/ppc_shm.py', 'tools/tests/ppc_fep/*', 'tools/tests/ppc_module/*',
            'tools/ppc/**', 'tools/*.py', 'tools/prototypes/fep_imports.def', 'tools/prototypes/sqlite_imports.def', 'Makefile', 'build/**', 'sdk/**', 'arch/ppc/**',
            'kdebug/**', 'platform/powermac/**', 'kernel/**', 'include/**', 'lib/**', 'fs/**', 'mm/**',
            'exec/**', 'init/**', 'input/**', 'gfx/**', 'sound/**', 'drivers/**', 'userland/**', 'net/**', 'kapi/**', 'assets/fonts/**'],
    basis=['docs/design/platform/powermac/root_pair.md'],
    env_limit='Requires mac99 QEMU and loopback sockets for link-sh.')


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD', 'SQLITE_PROTO_OUT',
                'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    rc = subprocess.call(['python3', '-B', 'tools/accept/ppc_fep.py'] +
                         (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
    return 0 if rc == 3 and env.get('OS32_REQUIRE_PPC') != '1' else rc
