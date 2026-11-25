"""Register tools/ppc/test_exec_entry.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-exec-entry',
    tier=1,
    budget=3,
    needs=[],
    covers=['tools/ppc/test_exec_entry.py',
     'sys/arch/ppc/exec_entry.c',
     'sys/arch/ppc/arch_exec_storage.h',
     'sys/include/exec_arch.h',
     'sys/include/types.h',
     'tools/checks/ppc_registered.py',
     'tools/tests/host32.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§3'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_exec_entry.py', ['gcc', 'qemu-i386'])
