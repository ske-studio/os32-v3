CHECK = dict(env=['ppc-cross', 'qemu-ppc'], name='arch-ctx-guest', tier=2, budget=900, needs=['make:kernel'],
             covers=['tools/accept/arch_ctx*', 'arch/ppc/**', 'platform/powermac/**', 'arch/x86/**',
                     'init/kernel.c', 'kernel/kstack_hw.[ch]', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'build/config.mk', 'build/out/*', 'kernel/idle_work.c',
                     'platform/pc98/tick_order.h', 'userland/tests/ring3_hello.c',
                     'userland/tests/ring3_marker.h', 'tools/np21w_mcp/*.py',
                     'exec/**', 'kernel/sys.c', 'kernel/irq/**', 'kernel/time/**',
                     'build/ppc-kboot.mk', 'build/ppc-user.mk', 'sdk/**',
                     'userland/ppc/**', 'include/**', 'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'tools/checks/arch-ctx-guest.py', 'init/ppc_kernel.c', 'mm/**', 'lib/**', 'build/arch/ppc.mk', 'tools/*.py', 'tools/ppc/**'],
             # Origin: ARCH_CTX_DESIGN AC-CTX-1..7 and its W4e acceptance row (PPC QEMU; x86 は NP21/W で PASS 2026-11-15).
             basis=['AC-CTX-1', 'AC-CTX-2', 'AC-CTX-5', 'AC-CTX-6', 'AC-CTX-7'])
def run(ctx):
    import os
    import subprocess
    env = dict(os.environ)
    for key in ("BUILD_OUT", "KERNEL_CFLAGS", "INC_KERNEL", "C_STD",
                "SQLITE_PROTO_OUT", "MAKEFLAGS", "MFLAGS", "MAKELEVEL"):
        env.pop(key, None)
    result = subprocess.call(['python3', '-B', 'tools/accept/arch_ctx.py', '--arch', 'x86', '--host'] +
                             (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
    if result: return result
    return subprocess.call(['python3', '-B', 'tools/accept/arch_ctx.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
