CHECK = dict(env=['ppc-cross', 'qemu-ppc'], name='arch-ctx-guest', tier=2, budget=900, needs=['make:ppc-ci-image'],
             covers=['tools/accept/arch_ctx*', 'arch/ppc/**', 'platform/powermac/**', 'arch/x86/**',
                     'sys/platform/pc98/kernel.c', 'kernel/kstack_hw.[ch]', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py', 'build/config.mk', 'build/out/*', 'build/out/ppc-user/**', 'sys/kern/idle_work.c',
                     'sys/platform/pc98/tick_order.h', 'userland/diag/ring3_hello.c',
                     'userland/diag/ring3_marker.h', 'tools/np21w_mcp/*.py',
                     'exec/**', 'sys/kern/sys.c', 'kernel/irq/**', 'kernel/time/**',
                     'build/targets/ppc-powermac.mk', 'build/ppc-user.mk', 'sdk/**',
                     'userland/ppc/**', 'include/**', 'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'tools/checks/arch-ctx-guest.py', 'sys/platform/powermac/ppc_kernel.c', 'mm/**', 'lib/**', 'build/arch/ppc.mk', 'tools/*.py', 'tools/ppc/**'],
             # Origin: ARCH_CTX_DESIGN AC-CTX-1..7 and its W4e acceptance row (PPC QEMU; x86 は NP21/W で PASS 2026-11-15).
             basis=['docs/tasks/ppc/TASK_PPC_ALIGN_X86.md B03/B09/B15', 'AC-CTX-1', 'AC-CTX-2', 'AC-CTX-5', 'AC-CTX-6', 'AC-CTX-7'])
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
