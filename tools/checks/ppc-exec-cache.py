"""Register tools/ppc/test_exec_cache.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-exec-cache',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_exec_cache.py',
     'arch/ppc/exec_cache.c',
     'arch/ppc/arch_exec_cache.h',
     'arch/ppc/ppc_oea.h',
     'build/ppc-kboot.mk',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§6'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_exec_cache.py', ['gcc', 'qemu-i386'])
