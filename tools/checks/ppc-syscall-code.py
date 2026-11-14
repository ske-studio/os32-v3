"""Register tools/ppc/test_syscall_code.py with its design expectations."""
CHECK = dict(
    name='ppc-syscall-code',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_syscall_code.py',
     'arch/ppc/syscall_code.c',
     'include/syscall_code.h',
     'exec/ring3_str.h',
     'exec/ring3_ls.h',
     'arch/ppc/exec_cache.h',
     'sdk/include/os32/os32_kapi_shared.h',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§4'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_syscall_code.py', ['gcc', 'qemu-i386'])
