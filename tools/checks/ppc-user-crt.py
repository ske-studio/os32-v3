"""Register tools/ppc/test_user_crt.py with its design expectations."""
CHECK = dict(
    name='ppc-user-crt',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_user_crt.py',
     'sdk/crt/ppc/crt0.S',
     'sdk/crt/ppc/start.c',
     'build/ppc-kboot.mk',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§3/§8'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_user_crt.py', ['gcc', 'qemu-i386'])
