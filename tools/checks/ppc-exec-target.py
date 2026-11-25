"""Register tools/ppc/test_exec_target.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-exec-target',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_exec_target.py',
     'sys/arch/ppc/arch_exec_image.h',
     'sys/arch/x86/arch_exec_image.h',
     'sys/kern/exec/os32x_hdr.c',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§6'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_exec_target.py', ['gcc', 'qemu-i386'])
