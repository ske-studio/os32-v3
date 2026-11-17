"""Register tools/ppc/test_app_link.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-app-link',
    tier=2,
    budget=60,
    needs=[],
    covers=['tools/ppc/test_app_link.py',
     'sdk/link/ppc/app.ld',
     'sdk/**',
     'sdk/link_guard.py',
     'sdk/include/os32/os32_unit_stamp.h',
     'sdk/crt/ppc/start.c',
     'sdk/mkos32x.py',
     'sdk/crt/ppc/crt0.S',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§8'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_app_link.py', ['gcc', 'qemu-i386'])
