"""Register tools/ppc/test_exec_publish.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-exec-publish',
    tier=1,
    budget=1,
    needs=[],
    covers=['tools/ppc/test_exec_publish.py',
     'sys/kern/exec/exec_image.inc',
     'sys/arch/ppc/arch_exec_cache.h',
     'sys/arch/x86/arch_exec_cache.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§6'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_exec_publish.py', ['gcc', 'qemu-i386'])
