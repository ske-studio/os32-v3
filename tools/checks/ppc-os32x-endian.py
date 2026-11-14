"""Register tools/ppc/test_os32x_endian.py with its design expectations."""
CHECK = dict(
    name='ppc-os32x-endian',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_os32x_endian.py',
     'sdk/os32x_hdr.py',
     'sdk/mkos32x.py',
     'sdk/**',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§6'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_os32x_endian.py', ['gcc', 'qemu-i386'])
