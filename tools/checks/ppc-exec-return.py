"""Register tools/ppc/test_exec_return.py with its design expectations."""
CHECK = dict(
    name='ppc-exec-return',
    tier=1,
    budget=3,
    needs=[],
    covers=['tools/ppc/test_exec_return.py',
     'arch/ppc/exec_context.c',
     'arch/ppc/arch_cpu.h',
     'arch/ppc/arch_exec_storage.h',
     'include/exec_arch.h',
     'include/types.h',
     'arch/ppc/ppc_frame.h',
     'arch/ppc/exec_resume.S',
     'lib/kstring.h',
     'lib/kstring_c.c',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§5'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_exec_return.py', ['gcc', 'qemu-i386'])
