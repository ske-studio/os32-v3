CHECK = dict(name='arch-ctx-guest', tier=2, budget=900, needs=['make:kernel'],
             covers=['tools/accept/arch_ctx*', 'arch/ppc/**', 'platform/powermac/**',
                     'exec/**', 'kernel/sys.c', 'kernel/irq/**', 'kernel/time/**',
                     'build/ppc-kboot.mk', 'build/ppc-user.mk', 'sdk/**',
                     'userland/ppc/**', 'include/**', 'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'tools/checks/arch-ctx-guest.py', 'init/ppc_kernel.c', 'mm/**', 'lib/**', 'build/arch/ppc.mk', 'tools/*.py', 'tools/ppc/**'],
             # Origin: ARCH_CTX_DESIGN AC-CTX-1..7 and its W4e acceptance row (PPC QEMU; x86 pending).
             basis=['AC-CTX-1', 'AC-CTX-2', 'AC-CTX-5', 'AC-CTX-6', 'AC-CTX-7'])
def run(ctx):
    import os
    import subprocess
    env = dict(os.environ)
    for key in ("BUILD_OUT", "KERNEL_CFLAGS", "INC_KERNEL", "C_STD",
                "SQLITE_PROTO_OUT", "MAKEFLAGS", "MFLAGS", "MAKELEVEL"):
        env.pop(key, None)
    return subprocess.call(['python3', '-B', 'tools/accept/arch_ctx.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
