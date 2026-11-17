"""Register tools/ppc/test_exec_enter.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-exec-enter',
    tier=1,
    budget=3,
    needs=[],
    covers=['tools/ppc/test_exec_enter.py',
     'arch/ppc/exec_enter.S',
     'arch/ppc/ppc_oea.h',
     'build/targets/ppc-powermac.mk',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§5'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_exec_enter.py', ['gcc', 'qemu-i386'])
