"""Register tools/ppc/test_setjmp.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-setjmp',
    tier=1,
    budget=10,
    needs=[],
    covers=['tools/ppc/test_setjmp.py',
     'arch/ppc/setjmp.S',
     'arch/ppc/arch_jmp_storage.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§5'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_setjmp.py', ['gcc', 'qemu-i386'])
