"""Register tools/ppc/test_user_trap.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-user-trap',
    tier=2,
    budget=120,
    needs=[],
    covers=['tools/ppc/test_user_trap.py',
     'tools/ppc/test_user_trap*',
     'sys/arch/ppc/kernel_trap.c',
     'sys/arch/ppc/pd_walk.c',
     'arch/ppc/ppc_*.h',
     'include/*.h',
     'tools/ppc/test_user_trap_host.c',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§5'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=True,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_user_trap.py', ['gcc', 'qemu-i386'])
