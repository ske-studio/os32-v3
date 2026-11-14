"""Register tools/ppc/test_hello_build.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-hello-build',
    tier=2,
    budget=60,
    needs=['make:ppc-ci-image'],
    covers=['tools/ppc/test_hello_build.py',
     'build/ppc-user.mk',
     'userland/ppc/hello.c',
     'userland/ppc/fault.c',
     'Makefile',
     'build/**',
     'sdk/**',
     'tools/checks/ppc_registered.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§8'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_hello_build.py', ['gcc', 'qemu-i386'])
