"""Register tools/ppc/test_exec_return.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-exec-return',
    tier=1,
    budget=3,
    needs=[],
    covers=['tools/ppc/test_exec_return.py',
     'sys/arch/ppc/exec_context.c',
     'sys/arch/ppc/arch_cpu.h',
     'sys/arch/ppc/arch_exec_storage.h',
     'sys/include/exec_arch.h',
     'sys/include/types.h',
     'sys/arch/ppc/ppc_frame.h',
     'sys/arch/ppc/exec_resume.S',
     'sys/kern/kstring.h',
     'sys/kern/kstring_c.c',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§5'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_exec_return.py', ['gcc', 'qemu-i386'])
