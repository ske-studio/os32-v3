"""Register tools/ppc/test_kapi_call.py with its design expectations."""
CHECK = dict(
    name='ppc-kapi-call',
    tier=1,
    budget=3,
    needs=[],
    covers=['tools/ppc/test_kapi_call.py',
     'arch/ppc/kapi_call.c',
     'arch/ppc/ppc_frame.h',
     'include/kapi_call.h',
     'include/exec_arch.h',
     'sdk/gen_kapi.py',
     'sdk/kapi.json',
     'tools/checks/ppc_registered.py',
     'tools/tests/host32.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§4'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_kapi_call.py', ['gcc', 'qemu-i386'])
