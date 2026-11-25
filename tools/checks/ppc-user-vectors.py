"""Register tools/ppc/test_user_vectors.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-user-vectors',
    tier=1,
    budget=10,
    needs=[],
    covers=['tools/ppc/test_user_vectors.py',
     'sys/arch/ppc/kernel_vectors.S',
     'sys/arch/ppc/ppc_frame.h',
     'sys/arch/ppc/ppc_oea.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§5'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_user_vectors.py', ['gcc', 'qemu-i386'])
